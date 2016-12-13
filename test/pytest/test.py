from rmtest import ModuleTestCase
import redis
import unittest
import json

docs = {
    'basic': {
        'key1': 'string value',
        'key2': None,
        'key#3': True,
        'k4': 42,
        'k5': [1, 2, 3],
        'k5.a': [42, None, -1.2, False, ['sub', 'array'], {'subdict': True}],
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


class JSONTestCase(ModuleTestCase(module_path='../../build/rejson.so', redis_path='../../../redis/src/redis-server')):

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
        # TODO
        pass

    def testTypeCommand(self):
        with self.redis() as r:
            r.delete('test')
            self.assertOk(r.execute_command('JSON.SET', 'test', '.', json.dumps(docs['types'])))
            for k, v in docs['types'].iteritems():
                reply = r.execute_command('JSON.TYPE', 'test', '.{}'.format(k))
                self.assertEqual(reply, k)

if __name__ == '__main__':
    unittest.main()
