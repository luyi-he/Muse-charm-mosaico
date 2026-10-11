import base64
import json
import unittest
from unittest.mock import patch
from gemini_audio import speak


class GeminiTests(unittest.TestCase):
    def test_pcm_alignment_and_completion_before_connection_close(self):
        played=[]
        class Response:
            status_code=200
            def __enter__(self):return self
            def __exit__(self,*args):pass
            def iter_lines(self):
                for chunk in (b'\x01',b'\x02\x03',b'\x04'):
                    yield 'data: '+json.dumps({'type':'step.delta','delta':{
                        'type':'audio','data':base64.b64encode(chunk).decode()}})
                yield 'data: '+json.dumps({'type':'interaction.completed'})
                raise AssertionError('must stop at completion without awaiting transport close')
        class Client:
            def __enter__(self):return self
            def __exit__(self,*args):pass
            def stream(self,*args,**kwargs):return Response()
        with patch('gemini_audio.httpx.Client',return_value=Client()):
            speak('test','test-key','Aoede',played.append,lambda:True)
        self.assertEqual(played,[b'\x01\x02',b'\x03\x04'])


if __name__=='__main__':unittest.main()
