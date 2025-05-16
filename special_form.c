#include "special_form.h"

#include <string.h>

#include "cons.h"
#include "evaluator.h"
#include "env.h"
#include "svalue.h"

static bool is_special_form(const char *token)
{
  return strcmp(token, "define") == 0 ||
         strcmp(token, "lambda") == 0 ||
         strcmp(token, "cond")   == 0 ||
         strcmp(token, "if")     == 0 ||
         strcmp(token, "and")    == 0 ||
         strcmp(token, "or")     == 0;
}

static SValue* from_string(const char *symbol)
{
  if (strcmp(symbol, "define") == 0)
    return SVALUE.special_form(SPECIAL_FORM_DEFINE);
  if (strcmp(symbol, "lambda") == 0)
    return SVALUE.special_form(SPECIAL_FORM_LAMBDA);
  if (strcmp(symbol, "cond") == 0)
    return SVALUE.special_form(SPECIAL_FORM_COND);
  if (strcmp(symbol, "if") == 0)
    return SVALUE.special_form(SPECIAL_FORM_IF);
  if (strcmp(symbol, "and") == 0)
    return SVALUE.special_form(SPECIAL_FORM_AND);
  if (strcmp(symbol, "or") == 0)
    return SVALUE.special_form(SPECIAL_FORM_OR);

  return SVALUE.errorf("unrecognized special form: \"%s\"", symbol);
}

static SValue* apply(SValue *sform, Rc* /*Env**/ env, SValue *args) {
  SpecialForm form = sform->val.special_form;
  SVALUE.release(&sform);
  switch (form) {
  case SPECIAL_FORM_DEFINE: 
    return SPECIAL_FORMS.define(env, args);
  case SPECIAL_FORM_LAMBDA:
    return SPECIAL_FORMS.lambda(env, args);
  case SPECIAL_FORM_COND:
    return SPECIAL_FORMS.cond(env, args);
  case SPECIAL_FORM_IF:
    return SPECIAL_FORMS._if(env, args);
  case SPECIAL_FORM_AND:
    return SPECIAL_FORMS.and(env, args);
  case SPECIAL_FORM_OR:
    return SPECIAL_FORMS.or(env, args);
  }

  return SVALUE.errorf("unrecognized special form");
}

static SValue* define(Rc* /*Env**/ env, SValue *args)
{
  size_t args_len = CONS.list.len(args);
  if (!args && args_len < 2) {
    return SVALUE.errorf("at least two arguments expected (define)");
  }

  SValue *head = CONS.car(args);
  SValue *body = CONS.cdar(args);

  if (SVAL_TYPE_SYMBOL == head->type) {
    if (args_len != 2) {
      return SVALUE.errorf("two arguments expected (define), got: %d\n", args_len);
    }
    // just a symbol - evaluate args beforehand and just store val
    SValue *sval = EVAL(env, body);
    if (SVAL_TYPE_ERR == sval->type) {
      return sval;
    }
    ENV.setnocopy(env, head->val.symbol, sval);
    SVALUE.release(&head);
    CONS.list.release_envelope(&args);
    return &SVAL_VOID;
  } else if (SVAL_TYPE_CONS == head->type) {
    SValue *name = CONS.car(head);
    if (SVAL_TYPE_SYMBOL != name->type) {
      SValue* res = SVALUE.errorf("expected symbol, got: %s (type=%s)",
                                  SVALUE.to_string(name),
                                  SVALUE_TYPE.to_string(name->type));
      SVALUE.release(&args);
      return res;
    }
    args->val.cons.car = args->val.cons.car->val.cons.cdr;
    SValue *func = SPECIAL_FORMS.lambda(env, args);
    if (SVAL_TYPE_ERR == func->type) {
      return func;
    }
    ENV.setnocopy(env, name->val.symbol, func);
    SVALUE.release(&name);
    free(head);
    return &SVAL_VOID;
  } else {
    return SVALUE.errorf("expected list or symbol");
  }
}

static SValue* lambda(Rc* /*Env**/ env, SValue *args)
{
  if (!CONS.is_list(args) || CONS.list.len(args) < 2) {
    return SVALUE.errorf("expected list at least 2 arguments (lambda) (got %d)",
                         CONS.list.len(args));
  }

  SValue *params = CONS.car(args);
  SValue *body = CONS.cdr(args);
  free(args);

  if (!CONS.is_list(params)) {
    return SVALUE.errorf("expected list. got: %s (type=%s)", 
                         SVALUE.to_string(params),
                         SVALUE_TYPE.to_string(params->type));
  }

  if (!CONS.list.is_all(params, SVALUE.is_symbol)) {
    return SVALUE.errorf("expected all parameters to be symbols (lambda)");
  }


  return SVALUE.scheme_func(params, body, env);
}

static SValue* cond(Rc* /*Env**/ env, SValue *args)
{
  if (!args) {
    return SVALUE.errorf("at least one condition/expression pair is expected (cond)");
  }

  SValue *head = args;
  SValue *res = &SVAL_VOID;
  while (head) {
    SValue *pair = CONS.car(head);
    SValue *next = CONS.cdr(head);
    free(head);
    head = next;
    if (!SVALUE.is_cons(pair)) {
      res = SVALUE.typeerr(pair, SVAL_TYPE_CONS);
      SVALUE.release(&pair);
      goto end;
    }

    SValue *cond = CONS.car(pair);
    SValue *expr = CONS.cdr(pair);
    free(pair);
    if (SVALUE.is_symbol(cond) && 
        strcmp(cond->val.symbol, "else") == 0) {
      SVALUE.release(&cond);
      if (next != NULL) {
        res = SVALUE.errorf("misplaced else clause - should be the last one in cond");
        SVALUE.release(&expr);
        goto end;
      }
    } else {
      SValue *cond_res = EVAL(env, cond);
      if (cond_res->type == SVAL_TYPE_ERR) {
        res = cond_res;
        SVALUE.release(&expr);
        goto end;
      }
      if (SVALUE.is_false(cond_res)) {
        SVALUE.release(&expr);
        SVALUE.release(&cond_res);
        continue;
      }
      SVALUE.release(&cond_res);
    }

    // condition is true - evaluate appropriate expression
    res = EVAL_ALL_BUT_ONE(env, expr);
    goto end;
  }
end:
  SVALUE.release(&head);
  return res;
}

static SValue* _if(Rc* /*Env**/ env, SValue *args)
{
  int arglen = CONS.list.len(args);
  if (arglen < 2) {
    SVALUE.release(&args);
    return SVALUE.errorf("if expected at least 2 arguments (if <condition> <then> [<else>]) (got %d)",
                         CONS.list.len(args));
  }

  SValue *cond = CONS.car(args);
  SValue *then = CONS.cdar(args);
  SValue *_else = arglen == 3 ? CONS.cddar(args) : NULL;

  CONS.list.release_envelope(&args);
  SValue *cond_res = EVAL(env, cond);

  if (cond_res->type == SVAL_TYPE_ERR) {
    SVALUE.release(&then);
    if (_else) { SVALUE.release(&_else); }
    return cond_res;
  }

  SValue *res;
  if (SVALUE.is_false(cond_res)) {
    SVALUE.release(&then);
    if (arglen == 2) // no else clause
      res = &SVAL_VOID;
    else
      res = _else;
  } else {
    if (_else) { SVALUE.release(&_else); }
    res = then;
  }

  SVALUE.release(&cond_res);
  return res;
}

static SValue* and(Rc* /*Env**/ env, SValue *args)
{
  if (!args)
    return SVALUE._bool(true);

  SValue *res = NULL;
  while (args) {
    SValue *car = CONS.car(args);
    SValue *cdr = CONS.cdr(args);
    free(args);
    args = cdr;
    if (res)
      SVALUE.release(&res);
    if (!args) { // last one, can be TCO'd
      res = car;
      break;
    }
    res = EVAL(env, car);
    if (SVALUE.is_false(res))
      goto end;

  }
end:
  SVALUE.release(&args);
  return res;
}

static SValue* or(Rc* /*Env**/ env, SValue *args)
{
  if (!args)
    return SVALUE._bool(false);

  SValue *res = NULL;
  while (args) {
    SValue *car = CONS.car(args);
    SValue *cdr = CONS.cdr(args);
    free(args);
    args = cdr;
    if (res)
      SVALUE.release(&res);
    if (!args) { // last one, can be TCO'd
      res = car;
      break;
    }
    res = EVAL(env, car);
    if (!SVALUE.is_false(res))
      goto end;

  }

end:
  SVALUE.release(&args);
  return res;
}

const struct _SpecialFormsStatic SPECIAL_FORMS = {
  .apply            = apply,
  .from_string      = from_string,
  .is_special_form  = is_special_form,

  .define           = define,
  .lambda           = lambda,
  .cond             = cond,
  ._if              = _if,
  .and              = and,
  .or               = or,
};
