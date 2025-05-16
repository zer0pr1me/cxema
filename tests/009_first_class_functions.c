#include "cxema.h"
#include "asserts.h"
#include "execs/file_interpreter.h"

int main() {
	Cxema *cx = CXEMA.form();
  SValue *sval = cx->interpret_file_all(cx, "tests/009_first_class_functions.scm");
	assert_type_equals(SVAL_TYPE_CONS, sval, __FILE__, __LINE__);

  Array *res = CONS.list.to_array(sval);

  char *strval = SVALUE.to_string(*(SValue **) res->get(res, 2));
  assert_str_equals("8", strval, __FILE__, __LINE__);
  free(strval);
  strval = SVALUE.to_string(*(SValue **) res->get(res, 3));
  assert_str_equals("1337", strval, __FILE__, __LINE__);
  free(strval);
  strval = SVALUE.to_string(*(SValue **) res->get(res, 5));
  assert_str_equals("1337", strval, __FILE__, __LINE__);
  free(strval);

  res->release(&res);
  cx->release(&cx);
	return 0;
}
