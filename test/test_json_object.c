#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "minunit.h"
#include "../src/json_object.h"

#define _JSTR(e) "\"" #e "\""

MU_TEST(testJSONObject) {
  const char SampleJSON[] =
        "{"
            _JSTR(foo) ": {"
                _JSTR(bar) ": ["
                    _JSTR(element0) ","
                    _JSTR(element1)
                    "],"
               _JSTR(inner object) ": {" \
                   _JSTR(baz) ":" _JSTR(qux)
               "}"
           "}"
        "}";

  Node *n1 = CreateNodeFromJSON(SampleJSON, strlen(SampleJSON), NULL);
  mu_check(n1);
  mu_check(N_DICT == n1->type);
  mu_check(1 == n1->value.dictval.len);

  Node *n2;
  mu_check(OBJ_ERR == Node_DictGet(n1, "f00", &n2));
  mu_check(OBJ_ERR == Node_DictGet(n1, "bar", &n2));
  mu_check(OBJ_ERR == Node_DictGet(n1, "baz", &n2));
  mu_check(OBJ_OK == Node_DictGet(n1, "foo", &n2));
  mu_check(N_DICT == n2->type);
  mu_check(2 == n2->value.dictval.len);
}

MU_TEST_SUITE(test_json_object) {

  MU_RUN_TEST(testJSONObject);
}

int main(int argc, char *argv[]) {
  MU_RUN_SUITE(test_json_object);
  MU_REPORT();
  return minunit_status;
}