/*
 *   
 *
 * Copyright  1990-2007 Sun Microsystems, Inc. All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 only, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details (a copy is
 * included at /legal/license.txt).
 * 
 * You should have received a copy of the GNU General Public License
 * version 2 along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 * 
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 or visit www.sun.com if you need additional
 * information or have any questions.
 */

class ThreadDescPointers: public MixedOopDesc {
protected:
  ThreadDesc*  _next;                  // 
  ThreadDesc*  _previous;              // 
  ThreadDesc*  _global_next;           // next thread in the global
                                       // thread list
  ThreadDesc*  _next_waiting;          // list of all threads waiting
  OopDesc*     _wait_obj;              // Object this thread is waiting for

  OopDesc*     _pending_exception;     // Contains the pending exception.
  OopDesc*     _thread_obj;            // java.lang.Thread mirror object.
  OopDesc*     _pending_entries;       // Points to the list of pending
                                       // entry activations
  OopDesc*     _execution_stack;       // Points to the execution stack
                                       // in the LWT case
  OopDesc*     _step_info;             // really a VMEventModifier object
  OopDesc*     _obj_value;             // for keeping return values
  OopDesc*     _async_info;            // byte[]: information about a 
                                       // thread that has called 
                                       // SNI_BlockThread.
  OopDesc*     _cached_async_info;     // byte[]: cached async info
  OopDesc*     _profiler_info;         // exact profiler's private data
#if ENABLE_JNI
  OopDesc*     _jni_frame;             // JNI frame
  OopDesc*     _local_references;      // array of local references
#endif

  friend class Thread;
  friend class Universe;
  friend class OopDesc;
};

class ThreadDesc: public ThreadDescPointers { 
 private:
  static size_t allocation_size() { 
    return align_allocation_size(sizeof(ThreadDesc));
  }
  static size_t pointer_count() {
    return (sizeof(ThreadDescPointers) - sizeof(MixedOopDesc)) /
           sizeof(OopDesc*);
  }

 private:
  jint         _id;                    // Thread ID for debugging purposes.
  address_word _last_java_sp_or_frame; // Native stack/frame pointer.
  address_word _last_java_fp;          // Native Java frame pointer.
  jint         _async_redo;            // Set to non-zero if
                                       // a native method needs redoing
  jint         _wait_stack_lock;       // Tells where to find the 
                                       // stack lock.
  jint         _task_id;               // Isolate task id

   // must be 8-byte-aligned on all platforms!
  jlong        _wakeup_time;           // When sleeping, contains the
                                       // wake-up time.
  address_word _stack_limit;           // The current native stack limit.
  address_word _stack_pointer;         // Native Java stack pointer.

  jint         _status;                // used by the debugger code
  jint         _suspend_count;         // used by debugger code
  jint         _int1_value;            // for keeping return values
  jint         _int2_value;            // for keeping return values
  
  friend class Thread;
  friend class Universe;
  friend class OopDesc;
};
