from rmtest import ModuleTestCase
import redis
import unittest
import json

class SearchTestCase(ModuleTestCase(module_path='../../build/rejson.so', redis_path='../../../redis/src/redis-server')):

    def testSetDocumentWithScalarShouldFail(self):
        with self.redis() as r, self.assertRaises(redis.exceptions.ResponseError):
            r.execute_command('json.set', 'test', '.', '1')
        self.assertFalse(r.exists('test'))

    def testSetWithEmptyDocumentShouldSucceed(self):
        with self.redis() as r:
            self.assertOk(r.execute_command('json.set', 'test', '.', json.dumps({})))
            self.assertExists(r, 'test')
            r.delete('test')

    def testSetGetDocumentShouldBeEqual(self):
        with self.redis() as r:
            data = json.dumps({
                'foo':  'bar',
                'baz':  [42, None, -1.2, False],
                'qux':  True,
                'zoo': {
                    'a':    1,
                    'b':    '2'
                }
            })
            self.assertOk(r.execute_command('json.set', 'test', '.', data))
            self.assertExists(r, 'test')
            self.assertEqual(json.dumps(json.loads(r.execute_command('json.get', 'test'))), data)
            r.delete('test')

if __name__ == '__main__':
    print "Hello"
    unittest.main()
