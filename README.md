nativethread
============

Allows for native function calls to be made from python in an interruptible
manner. 

`do_interruptibly(fnptr, ok_callback, cancel_callback, err_callback, arg)` accepts
a bare function pointer, three python callable objects, and arbitrary data
to pass to the given callbacks. `fnptr` will be called from a newly-spawned
non-Python thread, and an opaque handle for the new thread will be returned.

`interrupt(handle)` can be invoked on handles returned from the above function. 
The named thread will be brutally interrupted, and the `cancel_callback` will be
invoked. Any resources acquired by the native function will not be released
unless they are freed within the `cancel_callback`.

If the thread returns successfully, `ok_callback` will be invoked after the
native function finishes.
