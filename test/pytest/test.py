from rmtest import ModuleTestCase
import redis
import unittest
import json

docs = {
    'simple': {
        'foo': 'bar',
    },
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
            'c': None,
        }
    },
    'scalars': {
        'unicode': 'string value',
        'NoneType': None,
        'bool': True,
        'int': 42,
        'float': -1.2,
    },
    'values': {
        'unicode': 'string value',
        'NoneType': None,
        'bool': True,
        'int': 42,
        'float': -1.2,
        'dict': {},
        'list': []
    },
    'types': {
        'null':     None,
        'boolean':  False,
        'integer':  42,
        'number':   1.2,
        'string':   'str',
        'object':   {},
        'array':    [],
    },
}


class JSONTestCase(ModuleTestCase(module_path='../../lib/rejson.so', redis_path='../../../redis/src/redis-server')):
    # TODO: inject paths from upper

    def testSetRootWithNotObjectShouldFail(self):
        with self.redis() as r:
            r.delete('test')
            try:
                r.execute_command('JSON.SET', 'test', '.', '"string value"')
            except redis.exceptions.ResponseError:
                pass
            finally:
                self.assertFalse(r.exists('test'))

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.SET', 'test', '.', '1')

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.SET', 'test', '.', '-1.0101')

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.SET', 'test', '.', 'true')

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.SET', 'test', '.', 'null')

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.SET', 'test', '.', '"[1, 2, 3]"')

    def testSetRootWithEmptyObjectShouldSucceed(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', '{}'))
            self.assertExists(r, 'test')

    def testSetReplaceRootShouldSucceed(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', json.dumps(docs['basic'])))
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', json.dumps(docs['simple'])))
            raw = r.execute_command('JSON.GET', 'test', '.')
            self.assertDictEqual(json.loads(raw), docs['simple'])

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
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.GET', 'test', '.foo')

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.GET', 'test', '.key1[0]')

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.GET', 'test', '.key2.bar')

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.GET', 'test', '.key5[99]')

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.GET', 'test', '.key5["moo"]')

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.GET', 'test', '.bool', '.key5["moo"]')

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.GET', 'test', '.key5["moo"]', '.bool')

    def testGetPartsOfValuesDocumentOneByOne(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test',
                                            '.', json.dumps(docs['values'])))
            for k, v in docs['values'].iteritems():
                data = json.loads(r.execute_command('JSON.GET', 'test', '.{}'.format(k)))
                self.assertEqual(str(type(data)), '<type \'{}\'>'.format(k))
                self.assertEqual(data, v)

    def testGetPartsOfValuesDocumentMultiple(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test',
                                            '.', json.dumps(docs['values'])))
            data = json.loads(r.execute_command('JSON.GET', 'test', *docs['values'].keys()))
            self.assertDictEqual(data, docs['values'])

    def testMgetCommand(self):
        with self.redis() as r:
            for d in range(0, 5):
                key = 'doc:{}'.format(d)
                r.delete(key)
                self.assertOk(r.execute_command('JSON.SET', key, '.', json.dumps(docs['basic'])))

            raw = r.execute_command('JSON.MGET', '.', *['doc:{}'.format(d) for d in range(0, 5)])
            self.assertEqual(len(raw), 5)
            for d in range(0, 5):
                key = 'doc:{}'.format(d)
                self.assertDictEqual(json.loads(raw[d]), docs['basic'])

            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', '{"bool":false}'))
            raw = r.execute_command('JSON.MGET', '.bool', 'test', 'doc:0', 'foo')
            self.assertEqual(len(raw), 3)
            self.assertFalse(json.loads(raw[0]))
            self.assertTrue(json.loads(raw[1]))
            self.assertEqual(raw[2], None)

    def testDictionaryCRUD(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', '{ "dict": {} }'))
            raw = r.execute_command('JSON.GET', 'test', '.dict')
            data = json.loads(raw)
            self.assertDictEqual(data, {})

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.GET', 'test', '.dict.foo')

            self.assertOk(r.execute_command('JSON.SET', 'test', '.dict.foo', '"bar"'))
            raw = r.execute_command('JSON.GET', 'test', '.')
            data = json.loads(raw)
            self.assertDictEqual(data, {u'dict': {u'foo': u'bar'}})
            raw = r.execute_command('JSON.GET', 'test', '.dict')
            data = json.loads(raw)
            self.assertDictEqual(data, {u'foo': u'bar'})
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

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.SET', 'test', '.arr[9]', 0)

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.SET', 'test', '.arr[-9]', 0)

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

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.INSERT', 'test', '.arr', 0)

            # TODO: continue testing

    def testIndexCommand(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test',
                                            '.', '{ "arr": [0, 1, 2, 3, 2, 1, 0] }'))
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

    def testTrimCommand(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', 
                                            '.', '{ "arr": [0, 1, 2, 3, 2, 1, 0] }'))
            self.assertEqual(r.execute_command('JSON.TRIM', 'test', '.arr', 1, -2), 5)
            self.assertListEqual(json.loads(r.execute_command('JSON.GET', 'test', '.arr')), [1,2,3,2,1])
            self.assertEqual(r.execute_command('JSON.TRIM', 'test', '.arr', 0, 99), 5)
            self.assertListEqual(json.loads(r.execute_command('JSON.GET', 'test', '.arr')), [1,2,3,2,1])
            self.assertEqual(r.execute_command('JSON.TRIM', 'test', '.arr', 0, 2), 3)
            self.assertListEqual(json.loads(r.execute_command('JSON.GET', 'test', '.arr')), [1,2,3])
            self.assertEqual(r.execute_command('JSON.TRIM', 'test', '.arr', 99, 2), 0)
            self.assertListEqual(json.loads(r.execute_command('JSON.GET', 'test', '.arr')), [])

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
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.LEN', 'test', '.foo')

            # test an out of bounds index
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.LEN', 'test', '.arr[999]'), -1

            # test an infinite index
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.LEN', 'test', '.arr[-inf]')

    def testKeysCommand(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', json.dumps(docs['types'])))
            data = r.execute_command('JSON.KEYS', 'test', '.')
            self.assertEqual(len(data), len(docs['types']))
            for k in data:
                self.assertTrue(k in docs['types'])

    def testIncrCommand(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', '{ "foo": 0, "bar": "baz" }'))
            self.assertEqual(r.execute_command('JSON.INCRBY', 'test', '.foo', 1), 1)
            self.assertEqual(r.execute_command('JSON.INCRBY', 'test', '.foo', 2), 3)
            self.assertEqual(r.execute_command('JSON.INCRBY', 'test', '.foo', .5), '3.5')

            # test a wrong type
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.INCRBY', 'test', '.bar', 1)

            # test a missing path
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.INCRBY', 'test', '.fuzz', 1)


if __name__ == '__main__':
    unittest.main()
