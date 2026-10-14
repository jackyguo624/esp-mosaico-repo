"""Durable, per-device tasks. All writes and turn-cache commits are transactions."""
import json
import sqlite3
import threading
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

MAX_TASKS = 16

class Store:
    def __init__(self, path, timezone='Asia/Shanghai'):
        self.zone = ZoneInfo(timezone)
        self.lock = threading.RLock()
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.db.execute('PRAGMA journal_mode=WAL')
        self.db.executescript('''
        CREATE TABLE IF NOT EXISTS tasks (
            id INTEGER PRIMARY KEY, device TEXT NOT NULL, title TEXT NOT NULL,
            due INTEGER, repeat TEXT NOT NULL DEFAULT 'none', done INTEGER DEFAULT 0,
            notified_due INTEGER, created INTEGER NOT NULL);
        CREATE TABLE IF NOT EXISTS turns (
            device TEXT NOT NULL, turn_id TEXT NOT NULL, result TEXT NOT NULL,
            PRIMARY KEY(device, turn_id));
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY, device TEXT NOT NULL, role TEXT NOT NULL, content TEXT NOT NULL);
        ''')

    def close(self):
        self.db.close()

    def now(self):
        return datetime.now(self.zone)

    def tasks(self, device):
        with self.lock:
            rows = self.db.execute('SELECT * FROM tasks WHERE device=? AND done=0 '
                                   'ORDER BY due IS NULL, due, id LIMIT ?', (device, MAX_TASKS)).fetchall()
            return [dict(r) for r in rows]

    def history(self, device):
        with self.lock:
            rows = self.db.execute('SELECT role,content FROM messages WHERE device=? '
                                   'ORDER BY id DESC LIMIT 12', (device,)).fetchall()
            return [dict(r) for r in reversed(rows)]

    def cached(self, device, turn_id):
        with self.lock:
            row = self.db.execute('SELECT result FROM turns WHERE device=? AND turn_id=?',
                                  (device, turn_id)).fetchone()
            return json.loads(row[0]) if row else None

    def validate_add(self, item):
        title = item.get('title')
        if not isinstance(title, str) or not 1 <= len(title.strip()) <= 40 or len(title.strip().encode()) > 120:
            raise ValueError('任务标题须为 1–40 个字')
        repeat = item.get('repeat', 'none')
        if repeat not in ('none', 'daily', 'weekdays', 'weekly'):
            raise ValueError('不支持的重复周期')
        due = item.get('due')
        if due:
            if not isinstance(due, str):
                raise ValueError('提醒时间格式无效')
            when = datetime.fromisoformat(due)
            if when.tzinfo is None:
                raise ValueError('提醒时间须包含时区')
            delta = when.timestamp() - self.now().timestamp()
            if delta < -30 or delta > 366 * 2 * 86400:
                raise ValueError('请指定未来两年以内的提醒时间')
            due = int(when.timestamp())
        elif repeat != 'none':
            raise ValueError('重复提醒需要时间')
        else:
            due = None
        return title.strip(), due, repeat

    def apply_plan(self, device, turn_id, transcript, plan):
        """A failed action rolls back the entire plan; retrying a turn cannot add twice."""
        reply = plan.get('reply')
        if not isinstance(reply, str) or not reply.strip() or len(reply) > 300:
            raise ValueError('对话响应格式无效')
        actions = plan.get('actions', [])
        if not isinstance(actions, list) or len(actions) > 4:
            raise ValueError('任务操作格式无效')
        with self.lock, self.db:
            old = self.cached(device, turn_id)
            if old:
                return old
            confirmations = []
            for item in actions:
                if not isinstance(item, dict):
                    raise ValueError('任务操作格式无效')
                action = item.get('action')
                if action == 'add':
                    title, due, repeat = self.validate_add(item)
                    count = self.db.execute('SELECT COUNT(*) FROM tasks WHERE device=? AND done=0',
                                            (device,)).fetchone()[0]
                    if count >= MAX_TASKS:
                        raise ValueError('任务已满，请先完成一些任务')
                    self.db.execute('INSERT INTO tasks(device,title,due,repeat,created) VALUES(?,?,?,?,?)',
                                    (device, title, due, repeat, int(self.now().timestamp())))
                    when = datetime.fromtimestamp(due, self.zone).strftime('%m月%d日 %H:%M') if due else ''
                    cycle = {'none':'', 'daily':'，每天重复', 'weekdays':'，工作日重复', 'weekly':'，每周重复'}[repeat]
                    confirmations.append(f'已记录：{title}' + (f'，{when}提醒{cycle}。' if due else '。'))
                elif action in ('complete', 'delete'):
                    task = self._get(device, item.get('id'))
                    self._action(task, action, 0)
                    confirmations.append(('已完成：' if action == 'complete' else '已删除：') + task['title'] + '。')
                elif action == 'list':
                    tasks = self.tasks(device)
                    confirmations.append('当前任务：' + '；'.join(t['title'] for t in tasks) if tasks else '目前没有未完成任务。')
                else:
                    raise ValueError('不支持的任务操作')
            # Model prose must not invent successful mutations: for mutations, use executor confirmations.
            spoken = ' '.join(confirmations) if confirmations else reply.strip()
            emotion = plan.get('emotion', 'happy')
            if emotion not in ('happy', 'calm', 'curious'):
                emotion = 'calm'
            result = {'transcript': transcript, 'reply': spoken, 'emotion': emotion,
                      'tasks': self.tasks(device), 'speech_id': None}
            self.db.execute('INSERT INTO messages(device,role,content) VALUES(?,?,?)', (device, 'user', transcript))
            self.db.execute('INSERT INTO messages(device,role,content) VALUES(?,?,?)', (device, 'assistant', spoken))
            self.db.execute('DELETE FROM messages WHERE device=? AND id NOT IN '
                            '(SELECT id FROM messages WHERE device=? ORDER BY id DESC LIMIT 12)', (device, device))
            self.db.execute('INSERT INTO turns VALUES(?,?,?)', (device, turn_id, json.dumps(result, ensure_ascii=False)))
            # Keep a bounded cache; old IDs are UUIDs and will not be automatically replayed by the device.
            self.db.execute('DELETE FROM turns WHERE device=? AND rowid NOT IN '
                            '(SELECT rowid FROM turns WHERE device=? ORDER BY rowid DESC LIMIT 64)', (device, device))
            return result

    def _get(self, device, task_id):
        if type(task_id) is not int:
            raise ValueError('任务编号无效')
        row = self.db.execute('SELECT * FROM tasks WHERE device=? AND id=? AND done=0',
                              (device, task_id)).fetchone()
        if not row:
            raise ValueError('找不到这条任务')
        return dict(row)

    def _next_due(self, task):
        due = datetime.fromtimestamp(task['due'], self.zone)
        now = self.now()
        step = 7 if task['repeat'] == 'weekly' else 1
        # Advance from the scheduled wall clock, not from snooze completion or elapsed seconds.
        if due > now:
            due += timedelta(days=step)
        while due <= now:
            due += timedelta(days=step)
            if task['repeat'] == 'weekdays':
                while due.weekday() >= 5:
                    due += timedelta(days=1)
        if task['repeat'] == 'weekdays':
            while due.weekday() >= 5:
                due += timedelta(days=1)
        return int(due.timestamp())

    def _action(self, task, action, seconds):
        if action == 'complete' and task['repeat'] != 'none':
            self.db.execute('UPDATE tasks SET due=?,notified_due=NULL WHERE id=?', (self._next_due(task), task['id']))
        elif action in ('complete', 'delete'):
            self.db.execute('UPDATE tasks SET done=1 WHERE id=?', (task['id'],))
        elif action == 'snooze':
            if type(seconds) is not int or not 60 <= seconds <= 86400:
                raise ValueError('稍后提醒须在一分钟至一天以内')
            # Snooze must preserve the recurring schedule's wall clock. A separate event is added.
            if task['repeat'] != 'none':
                self.db.execute('UPDATE tasks SET due=?,notified_due=NULL WHERE id=?', (self._next_due(task),task['id']))
                self.db.execute('INSERT INTO tasks(device,title,due,repeat,created) VALUES(?,?,?,?,?)',
                                (task['device'], task['title'], int(self.now().timestamp()) + seconds,
                                 'none', int(self.now().timestamp())))
            else:
                self.db.execute('UPDATE tasks SET due=?,notified_due=NULL WHERE id=?',
                                (int(self.now().timestamp()) + seconds, task['id']))
        elif action == 'ack':
            self.db.execute('UPDATE tasks SET notified_due=due WHERE id=?', (task['id'],))
        else:
            raise ValueError('不支持的任务操作')

    def task_action(self, device, task_id, action, seconds=600):
        with self.lock, self.db:
            task = self._get(device, task_id)
            if action == 'snooze' and task['repeat'] != 'none' and len(self.tasks(device)) >= MAX_TASKS:
                raise ValueError('任务已满，暂时无法稍后提醒')
            self._action(task, action, seconds)
            return self.tasks(device)

    def due(self, device):
        now = int(self.now().timestamp())
        return [t for t in self.tasks(device) if t['due'] and t['due'] <= now
                and t['notified_due'] != t['due']]

    def rollover(self, device):
        """Repeat after a delivered occurrence without skipping a day on Complete.

        Keep the current delivered occurrence available for the user to complete
        or snooze. Roll to the latest scheduled occurrence only when it is due.
        """
        def advance(when, repeat):
            when += timedelta(days=7 if repeat == 'weekly' else 1)
            while repeat == 'weekdays' and when.weekday() >= 5:
                when += timedelta(days=1)
            return when
        with self.lock, self.db:
            rows = self.db.execute("SELECT * FROM tasks WHERE device=? AND done=0 AND repeat!='none' AND notified_due=due", (device,)).fetchall()
            now = self.now()
            for row in rows:
                task = dict(row)
                next_due = advance(datetime.fromtimestamp(task['due'], self.zone), task['repeat'])
                if next_due > now:
                    continue
                while advance(next_due, task['repeat']) <= now:
                    next_due = advance(next_due, task['repeat'])
                self.db.execute('UPDATE tasks SET due=?,notified_due=NULL WHERE id=?', (int(next_due.timestamp()), task['id']))
