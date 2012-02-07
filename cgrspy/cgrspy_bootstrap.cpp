#include <Python.h>
#include <structmember.h>
#undef HAVE_SYS_TYPES_H // Avoid warning
#include "IfaceCGRS.hxx"
#include "CGRSBootstrap.hpp"
#include "cellml-api-cxx-support.hpp"
#include <sstream>
#include <list>

typedef struct {
    PyObject_HEAD
    iface::XPCOM::IObject* mObject;
} Object;

typedef struct {
  PyObject_HEAD
  PyObject* asString;
  int asInteger;
} Enum;

typedef struct {
  PyObject_HEAD
  iface::CGRS::GenericMethod* mInvokeMethod;
  iface::CGRS::ObjectValue* mInvokeOn;
} Method;

static void ObjectDealloc(Object* self);
static void EnumDealloc(Enum* self);
static int EnumInit(Enum *self, PyObject *args, PyObject *kwds);
static PyObject *EnumNew(PyTypeObject *type, PyObject *args, PyObject *kwds);

static PyObject* genericValueToPython(iface::CGRS::GenericValue* aGenVal);
static already_AddRefd<iface::CGRS::GenericValue> pythonToGenericValue(PyObject* aObj, iface::CGRS::GenericType* aType);

static PyObject* objectGetAttr(PyObject* aObj, char* aName);
static int objectSetAttr(PyObject* aObj, char* aName, PyObject* aValue);
static PyObject* objectIterNext(PyObject *aObj);
static PyObject* objectGetIter(PyObject *aObj);

static int methodInit(Method* self, PyObject* args, PyObject* kwds);
static PyObject* methodNew(PyTypeObject *type, PyObject* args, PyObject* kwds);
static void methodDealloc(Method* self);
static PyObject* methodCall(Method* self, PyObject* args, PyObject* kwds);

class ScopedGIL
{
public:
  ScopedGIL()
  {
    mState = PyGILState_Ensure();
  }

  ~ScopedGIL()
  {
    PyGILState_Release(mState);
  }

private:
  PyGILState_STATE mState;
};

class PythonObjectType
  : public iface::CGRS::GenericType
{
public:
  PythonObjectType()
    : refcount(1)
  {
  }

  void add_ref() throw() { refcount++; }
  void release_ref() throw()
  {
    refcount--;
    if (refcount == 0)
      delete this;
  }
  std::string objid() throw()
  {
    return "XPCOM::IObjectPythonWrapper";
  }
  void* query_interface(const std::string& aIface) throw()
  {
    if (aIface == "XPCOM::IObject")
      return reinterpret_cast<void*>(static_cast<iface::XPCOM::IObject*>(this));
    else if (aIface == "CGRS::GenericType")
      return reinterpret_cast<void*>(static_cast<iface::CGRS::GenericType*>(this));
    return NULL;
  }
  std::vector<std::string> supported_interfaces() throw()
  {
    std::vector<std::string> iface;
    iface.push_back("XPCOM::IObject");
    iface.push_back("CGRS::GenericType");
    return iface;
  }
  std::string asString() throw()
  {
    return "XPCOM::IObject";
  }
private:
  int refcount;
};

class PythonCallback
  : public iface::CGRS::CallbackObjectValue
{
public:
  PythonCallback(PyObject* aPyObject)
    : mPyObject(aPyObject), refcount(1)
  {
    Py_INCREF(mPyObject);
  }

  ~PythonCallback()
  {
    Py_DECREF(mPyObject);
  }

  void add_ref() throw()
  {
    refcount++;
  }
  
  void release_ref() throw()
  {
    refcount--;
    if (refcount == 0)
      delete this;
  }

  std::string objid() throw()
  {
    std::string ret;
    ScopedGIL gil;
    long val = PyObject_Hash(mPyObject);
    ret += (val % 254) + 1;
    val /= 254;

    ret += (val % 254) + 1;
    val /= 254;

    ret += (val % 254) + 1;
    val /= 254;

    ret += (val % 254) + 1;
    val /= 254;

    ret += (val % 254) + 1;
    val /= 254;

    ret += (val % 254) + 1;
    val /= 254;

    ret += (val % 254) + 1;
    val /= 254;

    ret += (val % 254) + 1;
    val /= 254;

    ret += (val % 254) + 1;
    
    return ret;
  }

  void*
  query_interface(const std::string& aIface) throw()
  {
    add_ref();
    if (aIface == "XPCOM::IObject")
      return reinterpret_cast<void*>(static_cast<iface::XPCOM::IObject*>(this));
    else if (aIface == "CGRS::GenericValue")
      return reinterpret_cast<void*>(static_cast<iface::CGRS::GenericValue*>(this));
    else if (aIface == "CGRS::CallbackObjectValue")
      return reinterpret_cast<void*>(static_cast<iface::CGRS::CallbackObjectValue*>(this));
    release_ref();
    return NULL;
  }

  std::vector<std::string>
  supported_interfaces() throw()
  {
    std::vector<std::string> v;
    v.push_back("XPCOM::IObject");
    v.push_back("CGRS::CallbackObjectValue");
    return v;
  }

  PyObject* getObject()
  {
    Py_INCREF(mPyObject);
    return mPyObject;
  }

  already_AddRefd<iface::CGRS::GenericType> typeOfValue() throw()
  {
    return new PythonObjectType();
  }

  already_AddRefd<iface::CGRS::GenericValue>
  invokeOnInterface(const std::string& aInterfaceName, const std::string& aMethodName,
                    const std::vector<iface::CGRS::GenericValue*>& aInValues,
                    std::vector<iface::CGRS::GenericValue*>& aOutValues,
                    bool* aWasException
                    ) throw()
  {
    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());

    // One Python type can map to multiple GenericValue types, and so we have to
    // look up the type to produce the correct output...
    ObjRef<iface::CGRS::GenericInterface> gi;
    try { gi = cgs->getInterfaceByName(aInterfaceName); } catch (...) {}
    if (gi == NULL)
      return cgs->makeVoid();

    ObjRef<iface::CGRS::GenericMethod> gm;
    ObjRef<iface::CGRS::GenericAttribute> ga;
    try { ga = gi->getAttributeByName(aMethodName.c_str()); } catch (...) {}
    if (ga != NULL)
    {
      if (aInValues.size() == 0)
        gm = ga->getter();
      else
        gm = ga->setter();
    }
    else
    {
      try { gm = gi->getOperationByName(aMethodName); } catch (...) {}
    }

    if (gm == NULL)
      return cgs->makeVoid();

    ScopedGIL gil;

    PyObject* meth = PyObject_GetAttrString(mPyObject, aMethodName.c_str());
    if (meth == NULL)
    {
      PyErr_Clear();

      // Check for the explicitly named version...
      std::string ename;
      bool skipnext = false;
      for (std::string::const_iterator i = aInterfaceName.begin(); i != aInterfaceName.end(); i++)
      {
        if (skipnext)
        {
          skipnext = false;
          continue;
        }
        if (*i == ':')
        {
          ename += '_';
          skipnext = true;
        }
        else
          ename += *i;
      }
      ename += '_';
      ename += aMethodName;
      meth = PyObject_GetAttrString(mPyObject, ename.c_str());

      if (meth == NULL)
        return cgs->makeVoid();
    }

    std::vector<PyObject*> gen;
    PyObject* ptin = PyTuple_New(aInValues.size());
    int pos = 0;
    for (std::vector<iface::CGRS::GenericValue*>::const_iterator i = aInValues.begin(); i != aInValues.end(); i++)
      PyTuple_SET_ITEM(ptin, pos++, genericValueToPython(*i));
    
    PyObject* ret = PyObject_Call(meth, ptin, NULL);
    Py_DECREF(meth);
    Py_DECREF(ptin);

    if (PyErr_Occurred())
    {
      if (ret != NULL)
        Py_DECREF(ret);
      *aWasException = true;
      return cgs->makeVoid();
    }
    *aWasException = false;

    RETURN_INTO_OBJREF(gtret, iface::CGRS::GenericType, gm->returnType());
    if (!PyTuple_Check(ret))
    {
      iface::CGRS::GenericValue* gv = pythonToGenericValue(ret, gtret);
      Py_DECREF(ret);
      if (gv == NULL)
        return cgs->makeVoid();
      return gv;
    }

    PyObject* ret0 = PyTuple_GetItem(ret, 0);
    iface::CGRS::GenericValue* gvret = pythonToGenericValue(ret0, gtret);
    Py_DECREF(ret0);
    std::vector<iface::CGRS::GenericParameter*> gvpar = gm->parameters();

    size_t outi = 1, anyi = 0;
    for (; anyi < gvpar.size(); anyi++)
    {
      if (gvpar[anyi]->isOut())
      {
        PyObject* reti = PyTuple_GetItem(ret, outi++);
        ObjRef<iface::CGRS::GenericType> gvtype(gvpar[anyi]->type());
        aOutValues.push_back(pythonToGenericValue(reti, gvtype));
        Py_DECREF(reti);
      }
      gvpar[anyi]->release_ref();
    }

    if (gvret == NULL)
      return cgs->makeVoid();
    return gvret;
  }

private:
  PyObject* mPyObject;
  int refcount;
};

static PyTypeObject ObjectType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "cgrspy.Object",           /*tp_name*/
    sizeof(Object),            /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)ObjectDealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    objectGetAttr,             /*tp_getattr*/
    objectSetAttr,             /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "A cgrspy wrapped CellML API Object", /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    objectGetIter,             /* tp_iter */
    objectIterNext,            /* tp_iternext */
    NULL,                      /* tp_methods */
    NULL,                      /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                         /* tp_init */
    0,                         /* tp_alloc */
    0,                         /* tp_new */
};

static PyMemberDef Enum_members[] = {
  {const_cast<char*>("asString"), T_OBJECT_EX, offsetof(Enum, asString), 0, const_cast<char*>("Enumerator as string")},
  {const_cast<char*>("asInteger"), T_INT, offsetof(Enum, asInteger), 0,     const_cast<char*>("Enumerator as integer")},
  {NULL}
};

static PyTypeObject EnumType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "cgrspy.Enum",             /*tp_name*/
    sizeof(Enum),              /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)EnumDealloc,   /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "A cgrspy enum value",     /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    NULL,                      /* tp_methods */
    Enum_members,              /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)EnumInit,        /* tp_init */
    0,                         /* tp_alloc */
    EnumNew,                   /* tp_new */
};

static PyTypeObject MethodType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "cgrspy.Method",           /*tp_name*/
    sizeof(Method),            /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)methodDealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    (ternaryfunc)methodCall,   /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "A cgrspy native method",  /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    NULL,                      /* tp_methods */
    NULL,                      /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)methodInit,      /* tp_init */
    0,                         /* tp_alloc */
    methodNew,                 /* tp_new */
};

static PyObject*
genericValueToPythonB(iface::CGRS::GenericValue* aGenVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "boolean")
  {
    DECLARE_QUERY_INTERFACE_OBJREF(bv, aGenVal, CGRS::BooleanValue);
    if (bv->asBoolean())
    {
      Py_RETURN_TRUE;
    }
    else
    {
      Py_RETURN_FALSE;
    }
  }

  return NULL;
}

static PyObject*
genericValueToPythonC(iface::CGRS::GenericValue* aGenVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "char")
  {
    DECLARE_QUERY_INTERFACE_OBJREF(cv, aGenVal, CGRS::CharValue);
    char v = cv->asChar();
    return PyString_FromStringAndSize(&v, 1);
  }

  return NULL;
}

static PyObject*
genericValueToPythonD(iface::CGRS::GenericValue* aGenVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "double")
  {
    DECLARE_QUERY_INTERFACE_OBJREF(dv, aGenVal, CGRS::DoubleValue);
    return PyFloat_FromDouble(dv->asDouble());
  }

  return NULL;
}

static PyObject*
genericValueToPythonF(iface::CGRS::GenericValue* aGenVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "float")
  {
    DECLARE_QUERY_INTERFACE_OBJREF(fv, aGenVal, CGRS::FloatValue);
    return PyFloat_FromDouble(fv->asFloat());
  }

  return NULL;
}

static PyObject*
genericValueToPythonL(iface::CGRS::GenericValue* aGenVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "long")
  {
    DECLARE_QUERY_INTERFACE_OBJREF(lv, aGenVal, CGRS::LongValue);
    return PyInt_FromLong(lv->asLong());
  }
  else if (aTypename == "long long")
  {
    DECLARE_QUERY_INTERFACE_OBJREF(llv, aGenVal, CGRS::LongLongValue);
    return PyLong_FromLongLong(llv->asLongLong());
  }

  return NULL;
}

static PyObject*
genericValueToPythonO(iface::CGRS::GenericValue* aGenVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "octet")
  {
    DECLARE_QUERY_INTERFACE_OBJREF(ov, aGenVal, CGRS::OctetValue);
    return PyInt_FromLong(ov->asOctet());
  }

  return NULL;
}

static PyObject*
genericValueToPythonS(iface::CGRS::GenericValue* aGenVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "string")
  {
    DECLARE_QUERY_INTERFACE_OBJREF(sv, aGenVal, CGRS::StringValue);
    std::string s(sv->asString());
    return PyString_FromString(s.c_str());
  }
  else if (aTypename == "short")
  {
    DECLARE_QUERY_INTERFACE_OBJREF(sv, aGenVal, CGRS::ShortValue);
    return PyInt_FromLong(sv->asShort());
  }

  // Maybe it is a sequence...
  DECLARE_QUERY_INTERFACE_OBJREF(sv, aGenVal, CGRS::SequenceValue);
  if (sv != NULL)
  {
    long l = sv->valueCount();
    PyObject* lst = PyList_New(l);
    for (long i = 0; i < l; i++)
    {
      ObjRef<iface::CGRS::GenericValue> svi(sv->getValueByIndex(i));
      PyList_SET_ITEM(lst, i, genericValueToPython(svi));
    }
    return lst;
  }

  return NULL;
}

static PyObject*
genericValueToPythonU(iface::CGRS::GenericValue* aGenVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "unsigned short")
  {
    DECLARE_QUERY_INTERFACE_OBJREF(usv, aGenVal, CGRS::UShortValue);
    return PyInt_FromLong(usv->asUShort());
  }
  else if (aTypename == "unsigned long")
  {
    DECLARE_QUERY_INTERFACE_OBJREF(ulv, aGenVal, CGRS::ULongValue);
    return PyLong_FromUnsignedLong(ulv->asULong());
  }
  else if (aTypename == "unsigned long long")
  {
    DECLARE_QUERY_INTERFACE_OBJREF(ullv, aGenVal, CGRS::ULongLongValue);
    return PyLong_FromUnsignedLongLong(ullv->asULongLong());
  }

  return NULL;
}

static PyObject*
genericValueToPythonV(iface::CGRS::GenericValue* aGenVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "void")
  {
    Py_RETURN_NONE;
  }
  return NULL;
}

static PyObject*
genericValueToPythonW(iface::CGRS::GenericValue* aGenVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "wstring")
  {
    DECLARE_QUERY_INTERFACE_OBJREF(wsv, aGenVal, CGRS::WStringValue);
    std::stringstream ss;
    std::wstring ws(wsv->asWString());
    ss << ws;
    return PyString_FromString(ss.str().c_str());
  }
  return NULL;
}

static PyObject* (*fastG2PTypeTable[])(iface::CGRS::GenericValue* aGenVal, const std::string& aTypeName, iface::CGRS::GenericType* aGenType) = {
  /* a */ NULL,
  /* b */ genericValueToPythonB,
  /* c */ genericValueToPythonC,
  /* d */ genericValueToPythonD,
  /* e */ NULL,
  /* f */ genericValueToPythonF,
  /* g */ NULL,
  /* h */ NULL,
  /* i */ NULL,
  /* j */ NULL,
  /* k*/ NULL,
  /* l */ genericValueToPythonL,
  /* m */ NULL,
  /* n */ NULL,
  /* o */ genericValueToPythonO,
  /* p */ NULL,
  /* q */ NULL,
  /* r */ NULL,
  /* s */ genericValueToPythonS,
  /* t */ NULL,
  /* u */ genericValueToPythonU,
  /* v */ genericValueToPythonV,
  /* w */ genericValueToPythonW,
  /* x */ NULL,
  /* y */ NULL,
  /* z */ NULL
};

static void
ObjectDealloc(Object* self)
{
  if (self->mObject != NULL)
    self->mObject->release_ref();
  self->ob_type->tp_free((PyObject*)self);
}

static PyObject* Object_new(iface::XPCOM::IObject* aValue)
{
  Object* obj = PyObject_New(Object, &ObjectType);
  obj->mObject = aValue;
  obj->mObject->add_ref();

  return (PyObject*)obj;
}

static void EnumDealloc(Enum* self)
{
  Py_CLEAR(self->asString);
  self->ob_type->tp_free((PyObject*)self);
}

static int EnumInit(Enum *self, PyObject *args, PyObject *kwds)
{
  PyObject *asString = Py_None;

  static const char *kwlist[] = {"asString", "asInteger", NULL};

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "|Si", const_cast<char**>(kwlist), 
                                    &asString,
                                    &self->asInteger))
    return -1; 

  self->asString = asString;
  return 0;
}

static PyObject *EnumNew(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  Enum *self;

  self = (Enum*)type->tp_alloc(type, 0);
  if (self != NULL)
  {
    self->asString = Py_None;
    Py_INCREF(self->asString);
    self->asInteger = -1;
  }
  
  return (PyObject *)self;
}

static PyObject*
genericValueToPython(iface::CGRS::GenericValue* aGenVal)
{
  // Check it isn't a Python implemented object...
  DECLARE_QUERY_INTERFACE_OBJREF(cov, aGenVal, CGRS::CallbackObjectValue);

  if (cov != NULL)
  {
    PythonCallback* pycb = dynamic_cast<PythonCallback*>(static_cast<iface::CGRS::CallbackObjectValue*>(cov));
    
    if (pycb == NULL)
    {
      // We could support this case by making a wrapper that uses invokeOnInterface
      // to allow Python to call into a foreign callback, but it is an obscure corner
      // case in terms of the current API implementation, so lets just do this.
      PyErr_SetString(PyExc_ValueError, "Cannot convert a CGRS callback that didn't come from cgrspy to a Python object - this has not been implemented because we are not sure anyone would use it. File a tracker item against the CellML API at https://tracker.physiomeproject.org/ if you have a legitimate reason to want this.");
      return NULL;
    }

    return pycb->getObject();
  }

  // It could be an enum...
  DECLARE_QUERY_INTERFACE_OBJREF(ev, aGenVal, CGRS::EnumValue);
  if (ev != NULL)
  {
    Enum *evo = PyObject_New(Enum, &EnumType);
    std::string s(ev->asString());
    evo->asString = PyString_FromString(s.c_str());
    evo->asInteger = ev->asLong();
    return reinterpret_cast<PyObject*>(evo);
  }

  // Otherwise, it is one of the built-in types...
  ObjRef<iface::CGRS::GenericType> gt(aGenVal->typeOfValue());
  std::string n = gt->asString();
  char c = n[0];
  if (c < 'a' || c > 'z')
  {
    if (n == "XPCOM::IObject")
    {
      DECLARE_QUERY_INTERFACE_OBJREF(obj, aGenVal, CGRS::ObjectValue);
      ObjRef<iface::XPCOM::IObject> v(obj->asObject());
      return Object_new(v);
    }
    else
      return NULL;
  }
  c -= 'a';
  if (fastG2PTypeTable[(int)c] == NULL)
    return NULL;
  return fastG2PTypeTable[(int)c](aGenVal, n, gt);
}

static iface::CGRS::GenericValue*
pythonValueToGenericB(PyObject* aPyVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "boolean")
  {
    bool v = !!(PyInt_AsLong(aPyVal));
    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
    return cgs->makeBoolean(v);
  }

  return NULL;
}

static iface::CGRS::GenericValue*
pythonValueToGenericC(PyObject* aPyVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "char")
  {
    char * v = PyString_AsString(aPyVal);
    if (v == NULL)
      return NULL;
    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
    return cgs->makeChar(v[0]);
  }

  return NULL;
}

static iface::CGRS::GenericValue*
pythonValueToGenericD(PyObject* aPyVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "double")
  {
    double v = PyFloat_AsDouble(aPyVal);
    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
    return cgs->makeDouble(v);
  }

  return NULL;
}

static iface::CGRS::GenericValue*
pythonValueToGenericF(PyObject* aPyVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "float")
  {
    double v = PyFloat_AsDouble(aPyVal);
    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
    return cgs->makeFloat(static_cast<float>(v));
  }

  return NULL;
}

static iface::CGRS::GenericValue*
pythonValueToGenericL(PyObject* aPyVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "long")
  {
    int32_t v = PyInt_AsLong(aPyVal);
    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
    return cgs->makeLong(v);
  }
  else if (aTypename == "long long")
  {
    int64_t v = PyLong_AsLongLong(aPyVal);
    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
    return cgs->makeLongLong(v);
  }

  return NULL;
}

static iface::CGRS::GenericValue*
pythonValueToGenericO(PyObject* aPyVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "octet")
  {
    long v = PyInt_AsLong(aPyVal);
    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
    return cgs->makeOctet(static_cast<uint8_t>(v));
  }

  return NULL;
}

static iface::CGRS::GenericValue*
pythonValueToGenericS(PyObject* aPyVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "string")
  {
    char* sb = PyString_AsString(aPyVal);
    if (sb == NULL)
      return NULL;
    Py_ssize_t sl = PyString_Size(aPyVal);
    std::string s(sb, sl);
    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
    return cgs->makeString(s);
  }
  else if (aTypename == "short")
  {
    long v = PyInt_AsLong(aPyVal);
    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
    return cgs->makeShort(static_cast<int16_t>(v));
  }

  // Maybe it is a sequence...
  DECLARE_QUERY_INTERFACE_OBJREF(st, aGenType, CGRS::SequenceType);
  if (st != NULL)
  {
    Py_ssize_t l = PySequence_Length(aPyVal);
    if (l == -1)
      return NULL;

    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
    ObjRef<iface::CGRS::GenericType> innerType(st->innerType());
    ObjRef<iface::CGRS::SequenceValue> sv(cgs->makeSequence(innerType));
    for (Py_ssize_t i = 0; i < l; i++)
    {
      PyObject* pyItem = PySequence_GetItem(aPyVal, i);
      ObjRef<iface::CGRS::GenericValue> gitem = pythonToGenericValue(pyItem, innerType);
      Py_DECREF(pyItem);
      if (gitem == NULL)
        return NULL;

      sv->appendValue(gitem);
    }

    sv->add_ref();
    return sv;
  }

  return NULL;
}

static iface::CGRS::GenericValue*
pythonValueToGenericU(PyObject* aPyVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "unsigned short")
  {
    long v = PyInt_AsLong(aPyVal);
    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
    return cgs->makeUShort(static_cast<uint16_t>(v));
  }
  else if (aTypename == "unsigned long")
  {
    long long v = PyLong_AsLongLong(aPyVal);
    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
    return cgs->makeULong(static_cast<uint32_t>(v));
  }
  else if (aTypename == "unsigned long long")
  {
    long long v = PyLong_AsLongLong(aPyVal);
    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
    return cgs->makeULongLong(static_cast<uint64_t>(v));
  }

  return NULL;
}

static iface::CGRS::GenericValue*
pythonValueToGenericV(PyObject* aPyVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "void")
  {
    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
    return cgs->makeVoid();
  }
  return NULL;
}

static iface::CGRS::GenericValue*
pythonValueToGenericW(PyObject* aPyVal, const std::string& aTypename, iface::CGRS::GenericType* aGenType)
{
  if (aTypename == "wstring")
  {
    if (PyUnicode_Check(aPyVal))
      Py_INCREF(aPyVal);
    else
      aPyVal = PyUnicode_FromObject(aPyVal);

    if (aPyVal == NULL)
      return NULL;

    Py_ssize_t sl = PyUnicode_GetSize(aPyVal);
    if (PyErr_Occurred())
    {
      Py_DECREF(aPyVal);
      return NULL;
    }

    wchar_t buf[sl + 1];
    PyUnicode_AsWideChar(reinterpret_cast<PyUnicodeObject*>(aPyVal), buf, sl);

    Py_DECREF(aPyVal);

    std::wstring s(buf, sl);
    ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
    return cgs->makeWString(s);
  }

  return NULL;
}

static iface::CGRS::GenericValue* (*fastP2GTypeTable[])(PyObject* aPyVal, const std::string& aTypeName, iface::CGRS::GenericType* aGenType) = {
  /* a */ NULL,
  /* b */ pythonValueToGenericB,
  /* c */ pythonValueToGenericC,
  /* d */ pythonValueToGenericD,
  /* e */ NULL,
  /* f */ pythonValueToGenericF,
  /* g */ NULL,
  /* h */ NULL,
  /* i */ NULL,
  /* j */ NULL,
  /* k*/ NULL,
  /* l */ pythonValueToGenericL,
  /* m */ NULL,
  /* n */ NULL,
  /* o */ pythonValueToGenericO,
  /* p */ NULL,
  /* q */ NULL,
  /* r */ NULL,
  /* s */ pythonValueToGenericS,
  /* t */ NULL,
  /* u */ pythonValueToGenericU,
  /* v */ pythonValueToGenericV,
  /* w */ pythonValueToGenericW,
  /* x */ NULL,
  /* y */ NULL,
  /* z */ NULL
};

static already_AddRefd<iface::CGRS::GenericValue>
pythonToGenericValue(PyObject* aObj, iface::CGRS::GenericType* aType)
{
  ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
  std::string n = aType->asString();
  if (n == "XPCOM::IObject")
  {
    // See if aObj is a wrapped native object...
    if (PyObject_TypeCheck(aObj, &ObjectType))
      return cgs->makeObject((reinterpret_cast<Object*>(aObj))->mObject);

    // aObj is a Python object - wrap it in a callback.
    return new PythonCallback(aObj);
  }
  
  // It could be an enum...
  DECLARE_QUERY_INTERFACE_OBJREF(et, aType, CGRS::EnumType);
  if (et != NULL)
  {
    PyObject* iv = PyObject_GetAttrString(aObj, "asInteger");
    if (iv != NULL)
    {
      PyErr_Clear();
      long idx = PyInt_AsLong(iv);
      Py_DECREF(iv);
      if (!PyErr_Occurred())
        return cgs->makeEnumFromIndex(et, idx);
    }
    PyErr_Clear();

    iv = PyObject_GetAttrString(aObj, "asString");
    if (!iv)
      return NULL;
    char* str = PyString_AsString(iv);
    iface::CGRS::GenericValue* gv = NULL;
    if (str != NULL)
      gv = cgs->makeEnumFromString(et, str);
    Py_DECREF(iv);
    return gv;
  }

  // Our type should be a built-in type...
  char c = n[0];
  if (c < 'a' || c > 'z')
    return NULL; // Shouldn't happen.
  c -= 'a';
  if (fastP2GTypeTable[(int)c] == NULL)
    return NULL;
  return fastP2GTypeTable[(int)c](aObj, n, aType);
}

static PyObject*
objectGetAttr(PyObject* aObj, char* aName)
{
  iface::XPCOM::IObject* object = reinterpret_cast<Object*>(aObj)->mObject;
  ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
  ObjRef<iface::CGRS::GenericValue> gobject(cgs->makeObject(object));
  DECLARE_QUERY_INTERFACE_OBJREF(oobject, gobject, CGRS::ObjectValue);

  std::vector<std::string> v(object->supported_interfaces());
  for (std::vector<std::string>::iterator i = v.begin(); i != v.end(); i++)
  {
    ObjRef<iface::CGRS::GenericInterface> iface;
    try { iface = cgs->getInterfaceByName(*i); } catch (...) {}
    if (iface == NULL)
      continue;
    ObjRef<iface::CGRS::GenericAttribute> at;
    try { at = iface->getAttributeByName(aName); } catch (...) {}
    if (at != NULL)
    {
      ObjRef<iface::CGRS::GenericMethod> meth(at->getter());
      std::vector<iface::CGRS::GenericValue*> inseq, outseq;
      bool aWasException = false;
      ObjRef<iface::CGRS::GenericValue> ret(meth->invoke(oobject, inseq, outseq, &aWasException));
      if (aWasException)
      {
        PyErr_Format(PyExc_ValueError, "Exception raised by native CellML attribute getter %s on %s",
                     aName, (*i).c_str());
        return NULL;
      }
      return genericValueToPython(ret);
    }

    ObjRef<iface::CGRS::GenericMethod> meth;
    try { meth = iface->getOperationByName(aName); } catch (...) {}

    if (meth != NULL)
    {
      // We need to make a method object to return to Python...
      Method* pymeth = PyObject_New(Method, &MethodType);
      pymeth->mInvokeMethod = meth;
      meth->add_ref();
      pymeth->mInvokeOn = oobject;
      oobject->add_ref();
      return (PyObject*)pymeth;
    }
  }

  PyErr_Format(PyExc_ValueError, "%s: No such native CellML attribute or operation supported by object",
               aName);
  return NULL;
}

static int
objectSetAttr(PyObject* aObj, char* aName, PyObject* aValue)
{
  iface::XPCOM::IObject* object = reinterpret_cast<Object*>(aObj)->mObject;
  ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
  ObjRef<iface::CGRS::GenericValue> gobject(cgs->makeObject(object));
  DECLARE_QUERY_INTERFACE_OBJREF(oobject, gobject, CGRS::ObjectValue);

  std::vector<std::string> v(object->supported_interfaces());
  for (std::vector<std::string>::iterator i = v.begin(); i != v.end(); i++)
  {
    ObjRef<iface::CGRS::GenericInterface> iface;
    try { iface = (cgs->getInterfaceByName(*i)); } catch (...) {}
    if (iface == NULL)
      continue;
    ObjRef<iface::CGRS::GenericAttribute> at;
    try { at = iface->getAttributeByName(aName); } catch (...) {}
    if (at != NULL)
    {
      if (at->isReadonly())
        continue;

      ObjRef<iface::CGRS::GenericType> t(at->type());
      ObjRef<iface::CGRS::GenericMethod> meth(at->setter());
      ObjRef<iface::CGRS::GenericValue> arg(pythonToGenericValue(aValue, t));
      if (arg == NULL)
        return 1;
      std::vector<iface::CGRS::GenericValue*> inVec, outVec;
      inVec.push_back(arg);
      bool wasException = false;
      meth->invoke(oobject, inVec, outVec, &wasException)->release_ref();
      if (wasException)
      {
        PyErr_Format(PyExc_ValueError, "Exception raised while calling native CellML setter %s on interface %s",
                     aName, (*i).c_str());
        return 1;
      }
      return 0;
    }
  }

  PyErr_Format(PyExc_ValueError, "%s: No such native CellML setter",
               aName);
  return 1;
}

static PyObject* objectGetIter(PyObject* aObject)
{
  PyObject* objNext = objectGetAttr(aObject, (char*)"iterate");
  if (objNext == NULL)
    return NULL;
  PyObject* v = PyTuple_New(0);
  PyObject* ret = PyObject_Call(objNext, v, NULL);
  Py_DECREF(objNext);
  Py_DECREF(v);
  return ret;
}

static PyObject* objectIterNext(PyObject* aObject)
{
  PyObject* objNext = objectGetAttr(aObject, (char*)"next");
  if (objNext == NULL)
    return NULL;

  PyObject* v = PyTuple_New(0);
  PyObject* ret = PyObject_Call(objNext, v, NULL);
  Py_DECREF(v);
  Py_DECREF(objNext);

  if (ret == Py_None)
  {
    Py_DECREF(ret);
    return NULL;
  }

  return ret;
}

static int
methodInit(Method* self, PyObject* args, PyObject* kwds)
{
  return 0;
}

static PyObject*
methodNew(PyTypeObject *type, PyObject* args, PyObject* kwds)
{
  Method *self;
  self = (Method*)type->tp_alloc(type, 0);

  self->mInvokeMethod = NULL;
  self->mInvokeOn = NULL;

  return (PyObject*)self;
}

static void
methodDealloc(Method* self)
{
  if (self->mInvokeMethod != NULL)
    self->mInvokeMethod->release_ref();
  if (self->mInvokeOn != NULL)
    self->mInvokeOn->release_ref();
  self->ob_type->tp_free((PyObject*)self);
}

static PyObject*
methodCall(Method* self, PyObject* args, PyObject* kwds)
{
  if (self->mInvokeMethod == NULL || self->mInvokeOn == NULL)
  {
    PyErr_SetString(PyExc_ValueError, "cgrspy method not properly initialised");
    return NULL;
  }

  Py_ssize_t nargsActual = PySequence_Size(args);

  std::vector<iface::CGRS::GenericValue*> inVals, outVals;
  std::vector<iface::CGRS::GenericParameter*> parSeq(self->mInvokeMethod->parameters());

  Py_ssize_t nargsExpected = 0;
  for (std::vector<iface::CGRS::GenericParameter*>::iterator i = parSeq.begin();
       i != parSeq.end(); i++)
  {
    if ((*i)->isIn())
      nargsExpected++;
  }
  if (nargsExpected != nargsActual)
  {
    for (std::vector<iface::CGRS::GenericParameter*>::iterator i = parSeq.begin();
         i != parSeq.end(); i++)
      (*i)->release_ref();
    PyErr_Format(PyExc_ValueError, "Native CellML operation expected %ld arguments, but %ld were given",
                 nargsExpected, nargsActual);
    return NULL;
  }

  size_t itemIndex = 0;
  for (std::vector<iface::CGRS::GenericParameter*>::iterator i = parSeq.begin();
       i != parSeq.end(); i++)
  {
    if ((*i)->isIn())
    {
      PyObject* item = PySequence_GetItem(args, itemIndex);
      itemIndex++;
      ObjRef<iface::CGRS::GenericType> pt = (*i)->type();
      iface::CGRS::GenericValue* gitem = pythonToGenericValue(item, pt);
      Py_DECREF(item);
      if (gitem == NULL)
      {
        for (std::vector<iface::CGRS::GenericParameter*>::iterator i2 = parSeq.begin();
             i2 != parSeq.end(); i2++)
          (*i2)->release_ref();
        for (std::vector<iface::CGRS::GenericValue*>::iterator i2 = inVals.begin();
             i2 != inVals.end(); i2++)
          (*i2)->release_ref();
        return NULL;
      }
      inVals.push_back(gitem);
    }
  }

  for (std::vector<iface::CGRS::GenericParameter*>::iterator i = parSeq.begin();
       i != parSeq.end(); i++)
    (*i)->release_ref();

  bool wasException = false;
  ObjRef<iface::CGRS::GenericValue> retval(self->mInvokeMethod->invoke(self->mInvokeOn, inVals, outVals, &wasException));
  for (std::vector<iface::CGRS::GenericValue*>::iterator i = inVals.begin();
       i != inVals.end(); i++)
    (*i)->release_ref();
  
  if (wasException)
  {
    PyErr_SetString(PyExc_ValueError, "Native CellML operation raised exception");
    for (std::vector<iface::CGRS::GenericValue*>::iterator i = outVals.begin();
         i != outVals.end(); i++)
      (*i)->release_ref();
    return NULL;
  }

  std::list<iface::CGRS::GenericValue*> outList;
  outList.push_back(retval);
  outList.insert(outList.end(), outVals.begin(), outVals.end());
  retval->add_ref();

  ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());

  if (outList.size() == 1)
  {
    ObjRef<iface::CGRS::GenericValue> rv(already_AddRefd<iface::CGRS::GenericValue>(outList.front()));
    return genericValueToPython(rv);
  }

  PyObject* tuple = PyTuple_New(outList.size());
  itemIndex = 0;
  for (std::list<iface::CGRS::GenericValue*>::iterator i = outList.begin();
       i != outList.end(); i++)
  {
    // Note: SetItem steals a reference, so we don't need to Py_DECREF...
    PyTuple_SetItem(tuple, itemIndex++, genericValueToPython(*i));
    (*i)->release_ref();
  }

  return tuple;
}

static PyObject *
bootstrap_getBootstrap(PyObject *self, PyObject *args)
{
  const char* bsname;
  if (!PyArg_ParseTuple(args, "s", &bsname))
    return NULL;

  ObjRef<iface::CGRS::GenericsService> cgs(CreateGenericsService());
  ObjRef<iface::CGRS::GenericValue> gv;

  try
  {
    gv = cgs->getBootstrapByName(bsname);
  } catch (...) { /* Handled below... */}
  if (gv == NULL)
  {
    PyErr_Format(PyExc_LookupError, "Bootstrap %s could not be found", bsname);
    return NULL;
  }

  return genericValueToPython(gv);
}

static PyObject*
bootstrap_loadModule(PyObject* self, PyObject* args)
{
  PyEval_InitThreads();
  const char* bspath;
  if (!PyArg_ParseTuple(args, "s", &bspath))
    return NULL;

  already_AddRefd<iface::CGRS::GenericsService> cgs(CreateGenericsService());
  try
  {
    cgs->loadGenericModule(bspath);
  }
  catch (...)
  {
    PyErr_Format(PyExc_IOError, "Cannot load module from path %s", bspath);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyMethodDef BootstrapMethods[] = {
    {"fetch",  bootstrap_getBootstrap, METH_VARARGS,
     "Get a CGRS bootstrap object."},
    {"loadGenericModule", bootstrap_loadModule, METH_VARARGS,
     "Load a CGRS module."},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

PyMODINIT_FUNC
initbootstrap(void)
{
  PyObject *m;
  
  m = Py_InitModule("cgrspy.bootstrap", BootstrapMethods);
  if (m == NULL)
    return;

  PyType_Ready(&ObjectType);
  PyType_Ready(&EnumType);
  PyType_Ready(&MethodType);
}
