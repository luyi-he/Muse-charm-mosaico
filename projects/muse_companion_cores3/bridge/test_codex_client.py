import threading
import unittest
from unittest.mock import Mock
from codex_client import Codex, ThreadOwnedError


class OwnershipTests(unittest.TestCase):
    def client(self):
        c=Codex.__new__(Codex)
        c.operation=threading.Lock();c.selected='desktop-chat';c.thread='controller'
        c.rpc=Mock(side_effect=RuntimeError('thread desktop-chat already has an active writer'))
        c._ask=Mock();c.select=Mock()
        return c

    def test_busy_chat_never_starts_or_duplicates_request(self):
        c=self.client()
        with self.assertRaisesRegex(ThreadOwnedError,'尚未发送'):c.ask('收取最新邮件',lambda:True)
        c._ask.assert_not_called();c.select.assert_not_called()
        self.assertEqual(c.selected,'desktop-chat')

    def test_read_last_answer_does_not_resume_desktop_chat(self):
        c=self.client();c.history=Mock(return_value={'messages':[
            {'role':'assistant','text':'旧回复'},{'role':'user','text':'最新问题'},
            {'role':'assistant','text':'最新邮件摘要'}]})
        self.assertEqual(c.ask('读取当前会话最新回复。',lambda:True),'最新邮件摘要')
        c.rpc.assert_not_called();c._ask.assert_not_called()


if __name__=='__main__':unittest.main()
