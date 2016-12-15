from rmtest import ModuleTestCase
import redis
import unittest
import json

docs = {
    'basic': {
        'string': 'string value',
        'none': None,
        'bool': True,
        'int': 42,
        'num': 4.2,
        'arr': [42, None, -1.2, False, ['sub', 'array'], {'subdict': True}],
        'dict': {
            'a': 1,
            'b': '2',
            'c': None
        }
    },
    'scalars': {
        'unicode': 'string value',
        'NoneType': None,
        'bool': True,
        'int': 42,
        'float': -1.2
    },
    'types' : {
        'null':     None,
        'boolean':  False,
        'integer':  42,
        'number':   1.2,
        'string':   'str',
        'object':   {},
        'array':    []
    },
    'user': {
        'id':       'b2e4ded8a48cfeb837f300f78901fb278f29432c',
        'email':    'dfucbitz@soanon.ter',
        'signedIn': True,
        'lastSeen': '12/12/2016 00:32:04'
    },

}

# TODO: inject these from upper somehow
class JSONTestCase(ModuleTestCase(module_path='../../lib/rejson.so', redis_path='../../../redis/src/redis-server')):

    def testSetRootWithNotObjectShouldFail(self):
        with self.redis() as r:
            r.delete('test')
            try:
                r.execute_command('JSON.SET', 'test', '.', '"string value"')
            except redis.exceptions.ResponseError:
                pass
            finally:
                self.assertFalse(r.exists('test'))

            try:
                r.execute_command('JSON.SET', 'test', '.', '1')
            except redis.exceptions.ResponseError:
                pass
            finally:
                self.assertFalse(r.exists('test'))

            try:
                r.execute_command('JSON.SET', 'test', '.', '-1.0101')
            except redis.exceptions.ResponseError:
                pass
            finally:
                self.assertFalse(r.exists('test'))

            try:
                r.execute_command('JSON.SET', 'test', '.', 'true')
            except redis.exceptions.ResponseError:
                pass
            finally:
                self.assertFalse(r.exists('test'))

            try:
                r.execute_command('JSON.SET', 'test', '.', 'null')
            except redis.exceptions.ResponseError:
                pass
            finally:
                self.assertFalse(r.exists('test'))

            try:
                r.execute_command('JSON.SET', 'test', '.', '"[1, 2, 3]"')
            except redis.exceptions.ResponseError:
                pass
            finally:
                self.assertFalse(r.exists('test'))

    def testSetRootWithEmptyObjectShouldSucceed(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command(
                'JSON.SET', 'test', '.', json.dumps({})))
            self.assertExists(r, 'test')

    def testSetGetWholeBasicDocumentShouldBeEqual(self):
        with self.redis() as r:
            r.delete('test')
            data = json.dumps(docs['basic'])
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', data))
            self.assertExists(r, 'test')
            self.assertEqual(json.dumps(json.loads(
                r.execute_command('JSON.GET', 'test'))), data)

    def testGetNonExistantPathsFromBasicDocumentShouldFail(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test',
                                            '.', json.dumps(docs['scalars'])))
            try:
                r.execute_command('JSON.GET', 'test', '.foo')
            except redis.exceptions.ResponseError:
                pass
            finally:
                pass

            try:
                r.execute_command('JSON.GET', 'test', '.key1[0]')
            except redis.exceptions.ResponseError:
                pass
            finally:
                pass

            try:
                r.execute_command('JSON.GET', 'test', '.key2.bar')
            except redis.exceptions.ResponseError:
                pass
            finally:
                pass

            try:
                r.execute_command('JSON.GET', 'test', '.key5[99]')
            except redis.exceptions.ResponseError:
                pass
            finally:
                pass

            try:
                r.execute_command('JSON.GET', 'test', '.key5["moo"]')
            except redis.exceptions.ResponseError:
                pass
            finally:
                pass

    def testGetPartsOfScalarsDocument(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test',
                                            '.', json.dumps(docs['scalars'])))
            for k, v in docs['scalars'].iteritems():
                data = json.loads(r.execute_command(
                    'JSON.GET', 'test', '.{}'.format(k)))
                self.assertEqual(str(type(data)), '<type \'{}\'>'.format(k))
                self.assertEqual(data, v)

    def testDictionaryCRUD(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', '{ "dict": {} }'))
            raw = r.execute_command('JSON.GET', 'test', '.dict')
            data = json.loads(raw)
            self.assertDictEqual(data, {})

            try:
                r.execute_command('JSON.GET', 'test', '.dict.foo')
            except redis.exceptions.ResponseError:
                pass
            finally:
                pass

            self.assertOk(r.execute_command('JSON.SET', 'test', '.dict.foo', '"bar"'))
            raw = r.execute_command('JSON.GET', 'test', '.')
            data = json.loads(raw)
            self.assertDictEqual(data, {u'dict': { u'foo': u'bar' } })
            raw = r.execute_command('JSON.GET', 'test', '.dict')
            data = json.loads(raw)
            self.assertDictEqual(data, { u'foo': u'bar' })
            raw = r.execute_command('JSON.GET', 'test', '.dict.foo')
            data = json.loads(raw)
            self.assertEqual(data, u'bar')
            # TODO: continue testing

    def testArrayCRUD(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', '{ "arr": [] }'))
            self.assertOk(r.execute_command('JSON.SET', 'test', '.arr[-inf]', 0))
            self.assertOk(r.execute_command('JSON.SET', 'test', '.arr[+inf]', 1))
            self.assertOk(r.execute_command('JSON.SET', 'test', '.arr[0]', 0.1))
            self.assertOk(r.execute_command('JSON.SET', 'test', '.arr[-1]', 0.9))
            data = json.loads(r.execute_command('JSON.GET', 'test', '.arr'))
            self.assertListEqual(data, [0.1, 0.9])

            try:
                r.execute_command('JSON.SET', 'test', '.arr[9]', 0)
            except redis.exceptions.ResponseError:
                pass
            finally:
                pass

            try:
                r.execute_command('JSON.SET', 'test', '.arr[-9]', 0)
            except redis.exceptions.ResponseError:
                pass
            finally:
                pass


            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', '{ "arr": [1] }'))
            self.assertEqual(r.execute_command('JSON.INSERT', 'test', '.arr[0]', 0), 2)
            self.assertEqual(r.execute_command('JSON.INSERT', 'test', '.arr[-inf]', -1), 3)
            self.assertEqual(r.execute_command('JSON.INSERT', 'test', '.arr[+inf]', 2), 4)
            self.assertEqual(r.execute_command('JSON.INSERT', 'test', '.arr[-4]', -2), 5)
            self.assertEqual(r.execute_command('JSON.INSERT', 'test', '.arr[6]', 4), 6)
            self.assertEqual(r.execute_command('JSON.INSERT', 'test', '.arr[-1]', 3), 7)
            data = json.loads(r.execute_command('JSON.GET', 'test', '.arr'))
            self.assertListEqual(data, [-2, -1, 0, 1, 2, 3, 4])

            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', '{ "arr": [] }'))
            self.assertEqual(r.execute_command('JSON.INSERT', 'test', '.arr[0]', 2), 1)
            self.assertEqual(r.execute_command('JSON.INSERT', 'test', '.arr[-1]', 0, 1), 3)
            self.assertEqual(r.execute_command('JSON.INSERT', 'test', '.arr[-inf]', -2, -1), 5)
            self.assertEqual(r.execute_command('JSON.INSERT', 'test', '.arr[+inf]', 3, 6), 7)
            self.assertEqual(r.execute_command('JSON.INSERT', 'test', '.arr[6]', 4, 5), 9)
            data = json.loads(r.execute_command('JSON.GET', 'test', '.arr'))
            self.assertListEqual(data, [-2, -1, 0, 1, 2, 3, 4, 5, 6])
            
            try:
                r.execute_command('JSON.INSERT', 'test', '.arr', 0)
            except redis.exceptions.ResponseError:
                pass
            finally:
                pass
            # TODO: continue testing

    def testIndexCommand(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', '{ "arr": [0, 1, 2, 3, 2, 1, 0] }'))
            self.assertEqual(r.execute_command('JSON.INDEX', 'test', '.arr', 0), 0)
            self.assertEqual(r.execute_command('JSON.INDEX', 'test', '.arr', 3), 3)
            self.assertEqual(r.execute_command('JSON.INDEX', 'test', '.arr', 4), -1)
            self.assertEqual(r.execute_command('JSON.INDEX', 'test', '.arr', 0, 1), 6)
            self.assertEqual(r.execute_command('JSON.INDEX', 'test', '.arr', 0, -1), 6)
            self.assertEqual(r.execute_command('JSON.INDEX', 'test', '.arr', 0, 6), 6)
            self.assertEqual(r.execute_command('JSON.INDEX', 'test', '.arr', 0, 4, -1), 6)
            self.assertEqual(r.execute_command('JSON.INDEX', 'test', '.arr', 0, 5, -2), -1)
            self.assertEqual(r.execute_command('JSON.INDEX', 'test', '.arr', 2, -2, 6), -1)
            self.assertEqual(r.execute_command('JSON.INDEX', 'test', '.arr', '"foo"'), -1)

            self.assertEqual(r.execute_command('JSON.INSERT', 'test', '.arr[4]', '[4]'), 8)
            self.assertEqual(r.execute_command('JSON.INDEX', 'test', '.arr', 3), 3)
            self.assertEqual(r.execute_command('JSON.INDEX', 'test', '.arr', 2, 3), 5)
            self.assertEqual(r.execute_command('JSON.INDEX', 'test', '.arr', '[4]'), -1)

    def testTypeCommand(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', json.dumps(docs['types'])))
            for k, v in docs['types'].iteritems():
                reply = r.execute_command('JSON.TYPE', 'test', '.{}'.format(k))
                self.assertEqual(reply, k)

    def testLenCommand(self):
        with self.redis() as r:
            r.delete('test')

            # test that nothing is returned for empty keys 
            self.assertEqual(r.execute_command('JSON.LEN', 'foo', '.bar'), None)

            # test elements with valid lengths
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', json.dumps(docs['basic'])))
            self.assertEqual(r.execute_command('JSON.LEN', 'test', '.string'), 12)
            self.assertEqual(r.execute_command('JSON.LEN', 'test', '.dict'), 3)
            self.assertEqual(r.execute_command('JSON.LEN', 'test', '.arr'), 6)

            # test elements with undefined lengths
            self.assertEqual(r.execute_command('JSON.LEN', 'test', '.bool'), -1)
            self.assertEqual(r.execute_command('JSON.LEN', 'test', '.none'), -1)
            self.assertEqual(r.execute_command('JSON.LEN', 'test', '.int'), -1)
            self.assertEqual(r.execute_command('JSON.LEN', 'test', '.num'), -1)

            # test a non existing key
            try:
                self.assertEqual(r.execute_command('JSON.LEN', 'test', '.foo'), -1)
            except redis.exceptions.ResponseError:
                pass
            finally:
                pass
            
            # test an out of bounds index
            try:
                self.assertEqual(r.execute_command('JSON.LEN', 'test', '.arr[999]'), -1)
            except redis.exceptions.ResponseError:
                pass
            finally:
                pass

            # test an infinite index
            try:
                self.assertEqual(r.execute_command('JSON.LEN', 'test', '.arr[-inf]'), -1)
            except redis.exceptions.ResponseError:
                pass
            finally:
                pass

if __name__ == '__main__':
    unittest.main()
