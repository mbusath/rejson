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

    def testSetRootWithIllegalValuesShouldFail(self):
        with self.redis() as r:
            illegal = ['{', '}', '[', ']', '{]', '[}', '\\', '\\\\', '',
                       ' ', '\\"', '\'', '\[', '\x00', '\x0a', '\x0c', '\xff']
            r.delete('test')
            for i in illegal:
                with self.assertRaises(redis.exceptions.ResponseError) as cm:
                    r.execute_command('JSON.SET', 'test', '.', i)
                self.assertNotExists(r, 'test')

    def testSetRootWithJSONValuesShouldSucceed(self):
        """Test that the root of a JSON key can be set with any valid JSON"""
        with self.redis() as r:
            for v in ['"string"', '1', '-2', '3.14', 'null', 'true', 'false', '[]', '{}']:
                r.delete('test')
                self.assertOk(r.execute_command('JSON.SET', 'test', '.', v))
                self.assertExists(r, 'test')
                self.assertEqual(r.execute_command('JSON.GET', 'test'), v)

    def testSetReplaceRootShouldSucceed(self):
        """Test that replacing the root of an existing key with a valid object succeeds"""
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', json.dumps(docs['basic'])))
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', json.dumps(docs['simple'])))
            raw = r.execute_command('JSON.GET', 'test', '.')
            self.assertDictEqual(json.loads(raw), docs['simple'])
            for k, v in docs['values'].iteritems():
                self.assertOk(r.execute_command('JSON.SET', 'test', '.', json.dumps(v)))
                data = json.loads(r.execute_command('JSON.GET', 'test', '.'))
                self.assertEqual(str(type(data)), '<type \'{}\'>'.format(k))
                self.assertEqual(data, v)

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

    def testDelCommand(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', '{}'))
            self.assertEqual(r.execute_command('JSON.DEL', 'test', '.'), 1)
            self.assertNotExists(r, 'test')

            self.assertOk(r.execute_command('JSON.SET', 'test', '.', '{}'))
            self.assertOk(r.execute_command('JSON.SET', 'test', '.foo', '"bar"'))
            self.assertOk(r.execute_command('JSON.SET', 'test', '.baz', '"qux"'))
            self.assertEqual(r.execute_command('JSON.DEL', 'test', '.baz'), 1)
            self.assertEqual(r.execute_command('JSON.OBJLEN', 'test', '.'), 1)
            self.assertIsNone(r.execute_command('JSON.TYPE', 'test', '.baz'))
            self.assertEqual(r.execute_command('JSON.DEL', 'test', '.foo'), 1)
            self.assertEqual(r.execute_command('JSON.OBJLEN', 'test', '.'), 0)
            self.assertIsNone(r.execute_command('JSON.TYPE', 'test', '.foo'))
            self.assertEqual(r.execute_command('JSON.TYPE', 'test', '.'), 'object')

            self.assertOk(r.execute_command('JSON.SET', 'test', '.foo', '"bar"'))
            self.assertOk(r.execute_command('JSON.SET', 'test', '.baz', '"qux"'))
            self.assertOk(r.execute_command('JSON.SET', 'test', '.arr', '[1.2,1,2]'))

            self.assertEqual(r.execute_command('JSON.DEL', 'test', '.arr[1]'), 1)
            self.assertEqual(r.execute_command('JSON.OBJLEN', 'test', '.'), 3)
            self.assertEqual(r.execute_command('JSON.ARRLEN', 'test', '.arr'), 2)
            self.assertEqual(r.execute_command('JSON.TYPE', 'test', '.arr'), 'array')
            self.assertEqual(r.execute_command('JSON.DEL', 'test', '.arr'), 1)
            self.assertEqual(r.execute_command('JSON.OBJLEN', 'test', '.'), 2)
            self.assertEqual(r.execute_command('JSON.DEL', 'test', '.'), 1)
            self.assertIsNone(r.execute_command('JSON.GET', 'test'))

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
            self.assertEqual(r.execute_command('JSON.ARRINSERT', 'test', '.arr[0]', 0), 2)
            self.assertEqual(r.execute_command('JSON.ARRINSERT', 'test', '.arr[-inf]', -1), 3)
            self.assertEqual(r.execute_command('JSON.ARRINSERT', 'test', '.arr[+inf]', 2), 4)
            self.assertEqual(r.execute_command('JSON.ARRINSERT', 'test', '.arr[-4]', -2), 5)
            self.assertEqual(r.execute_command('JSON.ARRINSERT', 'test', '.arr[6]', 4), 6)
            self.assertEqual(r.execute_command('JSON.ARRINSERT', 'test', '.arr[-1]', 3), 7)
            data = json.loads(r.execute_command('JSON.GET', 'test', '.arr'))
            self.assertListEqual(data, [-2, -1, 0, 1, 2, 3, 4])

            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', '{ "arr": [] }'))
            self.assertEqual(r.execute_command('JSON.ARRINSERT', 'test', '.arr[0]', 2), 1)
            self.assertEqual(r.execute_command('JSON.ARRINSERT', 'test', '.arr[-1]', 0, 1), 3)
            self.assertEqual(r.execute_command('JSON.ARRINSERT', 'test', '.arr[-inf]', -2, -1), 5)
            self.assertEqual(r.execute_command('JSON.ARRINSERT', 'test', '.arr[+inf]', 3, 6), 7)
            self.assertEqual(r.execute_command('JSON.ARRINSERT', 'test', '.arr[6]', 4, 5), 9)
            data = json.loads(r.execute_command('JSON.GET', 'test', '.arr'))
            self.assertListEqual(data, [-2, -1, 0, 1, 2, 3, 4, 5, 6])

            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.ARRINSERT', 'test', '.arr', 0)

            # TODO: continue testing

    def testArrIndexCommand(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test',
                                            '.', '{ "arr": [0, 1, 2, 3, 2, 1, 0] }'))
            self.assertEqual(r.execute_command('JSON.ARRINDEX', 'test', '.arr', 0), 0)
            self.assertEqual(r.execute_command('JSON.ARRINDEX', 'test', '.arr', 3), 3)
            self.assertEqual(r.execute_command('JSON.ARRINDEX', 'test', '.arr', 4), -1)
            self.assertEqual(r.execute_command('JSON.ARRINDEX', 'test', '.arr', 0, 1), 6)
            self.assertEqual(r.execute_command('JSON.ARRINDEX', 'test', '.arr', 0, -1), 6)
            self.assertEqual(r.execute_command('JSON.ARRINDEX', 'test', '.arr', 0, 6), 6)
            self.assertEqual(r.execute_command('JSON.ARRINDEX', 'test', '.arr', 0, 4, -1), 6)
            self.assertEqual(r.execute_command('JSON.ARRINDEX', 'test', '.arr', 0, 5, -2), -1)
            self.assertEqual(r.execute_command('JSON.ARRINDEX', 'test', '.arr', 2, -2, 6), -1)
            self.assertEqual(r.execute_command('JSON.ARRINDEX', 'test', '.arr', '"foo"'), -1)

            self.assertEqual(r.execute_command('JSON.ARRINSERT', 'test', '.arr[4]', '[4]'), 8)
            self.assertEqual(r.execute_command('JSON.ARRINDEX', 'test', '.arr', 3), 3)
            self.assertEqual(r.execute_command('JSON.ARRINDEX', 'test', '.arr', 2, 3), 5)
            self.assertEqual(r.execute_command('JSON.ARRINDEX', 'test', '.arr', '[4]'), -1)

    def testArrTrimCommand(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test',
                                            '.', '{ "arr": [0, 1, 2, 3, 2, 1, 0] }'))
            self.assertEqual(r.execute_command('JSON.ARRTRIM', 'test', '.arr', 1, -2), 5)
            self.assertListEqual(json.loads(r.execute_command(
                'JSON.GET', 'test', '.arr')), [1, 2, 3, 2, 1])
            self.assertEqual(r.execute_command('JSON.ARRTRIM', 'test', '.arr', 0, 99), 5)
            self.assertListEqual(json.loads(r.execute_command(
                'JSON.GET', 'test', '.arr')), [1, 2, 3, 2, 1])
            self.assertEqual(r.execute_command('JSON.ARRTRIM', 'test', '.arr', 0, 2), 3)
            self.assertListEqual(json.loads(r.execute_command(
                'JSON.GET', 'test', '.arr')), [1, 2, 3])
            self.assertEqual(r.execute_command('JSON.ARRTRIM', 'test', '.arr', 99, 2), 0)
            self.assertListEqual(json.loads(r.execute_command('JSON.GET', 'test', '.arr')), [])

    def testTypeCommand(self):
        with self.redis() as r:
            for k, v in docs['types'].iteritems():
                r.delete('test')
                self.assertOk(r.execute_command('JSON.SET', 'test', '.', json.dumps(v)))
                reply = r.execute_command('JSON.TYPE', 'test', '.')
                self.assertEqual(reply, k)

    def testLenCommands(self):
        with self.redis() as r:
            r.delete('test')

            # test that nothing is returned for empty keys
            self.assertEqual(r.execute_command('JSON.ARRLEN', 'foo', '.bar'), None)

            # test elements with valid lengths
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', json.dumps(docs['basic'])))
            self.assertEqual(r.execute_command('JSON.STRLEN', 'test', '.string'), 12)
            self.assertEqual(r.execute_command('JSON.OBJLEN', 'test', '.dict'), 3)
            self.assertEqual(r.execute_command('JSON.ARRLEN', 'test', '.arr'), 6)

            # test elements with undefined lengths
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.ARRLEN', 'test', '.bool')
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.STRLEN', 'test', '.none')
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.OBJLEN', 'test', '.int')
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.STRLEN', 'test', '.num')

            # test a non existing key
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.LEN', 'test', '.foo')

            # test an out of bounds index
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.LEN', 'test', '.arr[999]'), -1

            # test an infinite index
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.LEN', 'test', '.arr[-inf]')

    def testObjKeysCommand(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', json.dumps(docs['types'])))
            data = r.execute_command('JSON.OBJKEYS', 'test', '.')
            self.assertEqual(len(data), len(docs['types']))
            for k in data:
                self.assertTrue(k in docs['types'])

            # test a wrong type
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.OBJKEYS', 'test', '.null')

    def testNumIncrCommand(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', '{ "foo": 0, "bar": "baz" }'))
            self.assertEqual(r.execute_command('JSON.NUMINCRBY', 'test', '.foo', 1), 1)
            self.assertEqual(r.execute_command('JSON.NUMINCRBY', 'test', '.foo', 2), 3)
            self.assertEqual(r.execute_command('JSON.NUMINCRBY', 'test', '.foo', .5), '3.5')

            # test a wrong type
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.NUMINCRBY', 'test', '.bar', 1)

            # test a missing path
            with self.assertRaises(redis.exceptions.ResponseError) as cm:
                r.execute_command('JSON.NUMINCRBY', 'test', '.fuzz', 1)


if __name__ == '__main__':
    unittest.main()
