#include <Python.h>
#include <pthread.h>

/************ Decls ************/


static PyObject* do_interruptably(PyObject* self, PyObject* args);
static PyObject* interrupt(PyObject* self, PyObject* args);

typedef struct ThreadInfo {
    PyObject* ok_cbak;
    PyObject* cancel_cbak;
    PyObject* err_cbak;
    PyObject* data;
    void* (*routine)(void);
} ThreadInfo;


/************ Module defs ************/


static const char THREAD_HANDLE_NAME[] = "nativethread.thread_handle";


static const char nativethread_doc[] = 
    "Run a native function on a non-python thread, such "
    "that it can be hard-cancelled.";


static PyMethodDef NativeThread_methods[] = {
    {"do_interruptably",  do_interruptably, METH_VARARGS,
     "Execute the given native function in a non-python thread.\n\n"
     
     "    do_interruptably(native_fnptr, ok_cbak, cancel_cbak, err_cbak, arg)\n\n"
     
     "Each `cbak` is a callable Python object. `ok_cbak` will be called if and\n"
     "when the native function returns normally. `cancel_cbak` will be called\n"
     "if the user cancels the new thread with `interrupt()`. `err_cbak` will\n"
     "be called if the spawned thread encounters a stack overflow or a segfault.\n"
     "The callbacks will be passed a single argument; the object given by `arg`.\n\n"
     
     "Returns: An opaque handle which can be passed to `interrupt()`.\n\n"
     
     "Buyer beware: Invalid native function pointers will cause a segfault."},
     
    {"interrupt", interrupt, METH_VARARGS,
     "Make a system call to interrupt the thread named by an opaque handle\n"
     "produced by `do_interruptably()`; the named thread will be brutally\n"
     "stopped mid-execution. Any resources acquired by that thread will\n"
     "not be freed unless they are released by the cancel callback passed\n"
     "to `do_interruptably()`."},
    
    {NULL, NULL, 0, NULL}        /* Sentinel */
};


static struct PyModuleDef nativethread_module = {
    PyModuleDef_HEAD_INIT,
    "nativethread",   /* name of module */
    nativethread_doc, /* module documentation, may be NULL */
    -1,               /* size of per-interpreter state of the module,
                         or -1 if the module keeps state in global variables. */
    NativeThread_methods
};


/************ Helper functions ************/


void handler_exec(ThreadInfo* thr, PyObject* routine) {
    /* get the GIL */
    PyGILState_STATE gstate = PyGILState_Ensure();
    
    /* do the user's finish callback */
    PyObject* args = Py_BuildValue("(O)", thr->data);
    PyObject* ret  = args ? PyObject_CallObject(routine, args) : NULL;
    
    /* release the python things */
    if (args) Py_DECREF(args);
    if (ret)  Py_DECREF(ret);
    Py_DECREF(thr->ok_cbak);
    Py_DECREF(thr->cancel_cbak);
    Py_DECREF(thr->err_cbak);
    Py_DECREF(thr->data);
    
    /* ok, done with GIL */
    PyGILState_Release(gstate);
    
    /* release the other thing */
    free(thr);
}


void nativethread_cancel_handler(void* data) {
    ThreadInfo* thr = (ThreadInfo*) data;
    handler_exec(thr, thr->cancel_cbak);
}


void* nativethread_run_thread(void* data) {
    /* set up the thread state, call the user's routine, and 
       call the user's `finish` callback when it's done. */
    int old_state = 0;
    ThreadInfo* thr = (ThreadInfo*) data;
    
    /* register the cancel callback and enable cancellation */
    pthread_cleanup_push(nativethread_cancel_handler, data);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_state);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_state);
    
    /* do the user's thing, or whatever */
    thr->routine();
    
    /* disable canceling. We don't want to permanently interrupt the
       execution of our finish function with a cancellation. 
       that would likely break python for good. */
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);
    pthread_cleanup_pop(0);
    
    /* do the python handler */
    handler_exec(thr, thr->ok_cbak);
    
    return NULL;
}


PyMODINIT_FUNC PyInit_nativethread(void) {
    return PyModule_Create(&nativethread_module);
}


static void destroy_thread_handle(PyObject* handle) {
    pthread_t* thr = (pthread_t*) PyCapsule_GetPointer(handle, THREAD_HANDLE_NAME);
    if (thr) free(thr);
}


static PyObject* make_thread_handle(pthread_t id) {
    /* make a new opaque handle for the python user to refer to the thread */
    /* we are probably just wrapping some kind of integer, but it's best 
       if the user can't just "make up" values to give to us. They
       explicitly have to get the handle from us. */
    pthread_t* thr = malloc(sizeof(pthread_t));
    if (!thr) return PyErr_NoMemory();
    
    *thr = id;
    return PyCapsule_New(thr, THREAD_HANDLE_NAME, destroy_thread_handle);
}


/************ Module functions ************/


static PyObject* interrupt(PyObject* self, PyObject* args) {
    PyObject* handle = NULL;
    /* extract args */
    if (!PyArg_ParseTuple(args, "O", &handle)) {
        return NULL;
    }
    if (!PyCapsule_CheckExact(handle)) {
        PyErr_SetString(PyExc_TypeError, "expected nativethread.thread_handle");
        return NULL;
    }
    
    /* extract the thread id */
    pthread_t* thr_handle = (pthread_t*) PyCapsule_GetPointer(handle, THREAD_HANDLE_NAME);
    if (!thr_handle) return NULL;
    pthread_t thr_id = *thr_handle;
    
    /* actually try to cancel the thread */
    if (pthread_cancel(thr_id)) {
        PyErr_SetString(PyExc_SystemError, "thread could not be cancelled (no such thread)");
        return NULL;
    }
    
    /* finished ok */
    Py_INCREF(Py_None);
    return Py_None;
}


/* arguments are: 
   native_function_ptr, ok_cbak, cancel_cbak, err_cbak, arg */
static PyObject* do_interruptably(PyObject* self, PyObject* args) {
    long long      native_fnptr;
    int            failed;
    pthread_attr_t thr_attrs;
    pthread_t      thr_id;
    
    /* allocate a structure to keep our callback info */
    ThreadInfo* thr = malloc(sizeof(ThreadInfo));
    if (!thr) return PyErr_NoMemory();
    
    /* populate that puppy */
    if (!PyArg_ParseTuple(
            args, "LOOOO",
            &native_fnptr, 
            &thr->ok_cbak, 
            &thr->cancel_cbak, 
            &thr->err_cbak, 
            &thr->data)) {
        free(thr);
        return NULL;
    }
    
    /* type check args */
    if (!(PyCallable_Check(thr->ok_cbak) && 
          PyCallable_Check(thr->cancel_cbak) &&
          PyCallable_Check(thr->err_cbak))) {
        PyErr_SetString(PyExc_TypeError, "arguments 2, 3, and 4 must be callable");
        free(thr);
        return NULL;
    }
    
    /* take ownership of the callbacks; store them for later */
    Py_INCREF(thr->ok_cbak);
    Py_INCREF(thr->cancel_cbak);
    Py_INCREF(thr->err_cbak);
    Py_INCREF(thr->data);
    thr->routine = (void* (*)(void))native_fnptr; /* ugh */
    
    /* start the thread; continue each step only if no errors */
    failed = pthread_attr_init(&thr_attrs);
    failed = failed ? failed : pthread_attr_setdetachstate(&thr_attrs, PTHREAD_CREATE_DETACHED);
    failed = failed ? failed : pthread_create(&thr_id, &thr_attrs, nativethread_run_thread, thr);
    
    pthread_attr_destroy(&thr_attrs);
    
    if (failed) {
        if (failed == ENOMEM) {
            PyErr_NoMemory();
        } else if (failed == EAGAIN) {
            PyErr_SetString(PyExc_SystemError, 
                "system could not allocate resources for a new thread");
        } else if (failed == EPERM) {
            PyErr_SetString(PyExc_SystemError,
                "could not start thread: insufficient permissions");
        } else {
            PyErr_SetString(PyExc_SystemError, "could not start system thread");
        }
        free(thr);
        return NULL;
    }
    
    return make_thread_handle(thr_id);
}

