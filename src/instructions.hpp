#pragma once

#include <list>

namespace rev {

  int64_t* jump(int64_t off);

  template<typename T>
  inline thread_t& operator<<(thread_t& t, const T& x) {
    t.push_back((thread_t::value_type) x);
    return t;
  }

  namespace instr {

    namespace stack {

      template<typename T = int64_t>
      inline T pop(stack_t& s) {
        return (T) *(--s);
      }

      template<typename T = int64_t>
      inline T top(stack_t& s) {
        return (T) *(s - 1);
      }

      template<typename T>
      inline void push(stack_t& s, const T& x) {
        *s++ = reinterpret_cast<int64_t>(x);
      }
    }

    void push(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "push: " << *ip << std::endl;
#endif
      stack::push(s, *ip++);
    }

    void return_here(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "return_here: " << *ip << std::endl;
#endif
      auto off = *(ip++);
      stack::push(s, (int64_t) (ip + off));
      stack::push(s, (int64_t) fp);
    }

    void return_to(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "return_to" << std::endl;
#endif
      auto ret  = stack::pop<>(s);
      // reset stack to calling function
      s         = fp;
      fp        = stack::pop<stack_t>(s);
      auto addr = stack::pop<int64_t*>(s);
      // set instruction pointer to return address
      ip = addr;

      stack::push(s, ret);
    }

    void pop(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "pop" << std::endl;
#endif
      stack::pop(s);
    }

    void br(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "br" << std::endl;
#endif
      ip += *ip;
    }

    void brcond(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "brcond" << std::endl;
#endif
      auto cond = stack::pop<value_t::p>(s);
      auto eoff = *(ip++);

      ip += is_truthy(cond) ? 0 : eoff;
    }

    void bind(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "bind: " << *ip << std::endl;
#endif
      auto var = (var_t::p) *ip++;
      auto val = stack::pop<value_t::p>(s);
      var->bind(val);
    }

    namespace priv {
      value_t::p adapt_runtime_types(
        const type_t::p& type, const std::list<value_t::p>& args) {

        return nullptr;
      }
    }

    void make(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "make" << std::endl;
#endif
      auto type = (type_value_t::p) *(ip++);

      std::list<value_t::p> tmp;
      for (auto i=0; i<imu::count(type->fields()); ++i) {
        auto val = stack::pop<value_t::p>(s);
        tmp.push_front(val);
      }

      // TODO: adapt runtime types

      auto val = imu::nu<rt_value_t>(type, vector_t::from_std(tmp));
      stack::push(s, val);
    }

    template<typename T>
    void make_native(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "make" << std::endl;
#endif
      auto n = *(ip++);

      std::list<value_t::p> tmp;
      for (auto i=0; i<n; ++i) {
        auto val = stack::pop<value_t::p>(s);
        tmp.push_front(val);
      }

      stack::push(s, T::from_std(tmp));
    }

    void set(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "poke" << std::endl;
#endif
      auto x   = stack::pop<rt_value_t::p>(s);
      auto val = stack::top<value_t::p>(s);
      auto sym = as<sym_t>((value_t::p) *ip++);

      x->set(sym, val);
    }

    void deref(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "deref: " << *ip << std::endl;
#endif
      auto var = (var_t::p) *(ip++);
      stack::push(s, (int64_t) var->deref());
    }

    void poke(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "poke" << std::endl;
#endif
      auto x   = stack::pop<rt_value_t::p>(s);
      auto sym = as<sym_t>((value_t::p) *ip++);
      stack::push(s, x->field(sym));
    }

    void enclosed(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "enclosed" << std::endl;
#endif
      auto v = (value_t::p) *fp;

      if (auto fn = as_nt<fn_t>(v)) {
        stack::push(s, fn->_closed_overs[*ip++]);
      }
      else if (auto val = static_cast<rt_value_t::p>(v)) {
        stack::push(s, val->field(*ip++));
      }
    }

    void closure(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "closure" << std::endl;
#endif
      auto address   = *ip++;
      auto enclosed  = *ip++;
      auto max_arity = (uint8_t) *ip++;
      auto fn        = imu::nu<fn_t>(address, max_arity);

      for (auto i=0; i<enclosed; ++i) {
        fn->enclose(stack::pop<value_t::p>(s));
      }

      stack::push(s, fn);
    }

    void dispatch(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "dispatch" << std::endl;
#endif
      auto arity = *(ip++);

      // set frame pointer to beginning of function in stack
      fp = (s - (arity + 1));

      auto f = as<fn_t>((value_t::p) *fp);

#ifdef _DEBUG
      assert(!f->is_macro());
#endif

      // FIXME: it's quite ugly to obtain the jump address this way,
      // but for now i can't think of anything better. real memory
      // addresses can't be obtained while building the code, since
      // compiling may invalidate existing addresses at any time.
      ip = jump(f->_code);

      if (arity > f->max_arity() && *(ip + f->max_arity() + 1) != -1) {

        auto rest = imu::nu<list_t>();
        while(arity-- > f->max_arity()) {
          rest = imu::conj(rest, stack::pop<value_t::p>(s));
        }
        stack::push(s, rest);

        arity = f->max_arity() + 1;
      }

      auto off = ip + arity;
      if (*off != -1) {
        ip += *off;
      }
      // TODO: emit list as IFn protocol call
      // TODO: emit list as native call
      else {
        throw std::runtime_error("Arity mismatch when calling fn");
      }
    }

    void method(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "method" << std::endl;
#endif
      auto proto = as<protocol_t>(stack::pop<value_t::p>(s));
      auto meth  = *(ip++);
      auto arity = *(ip++);

      void* args[arity];

      // arguments are on the stack in reverse order, so we
      // can pop arguments front to back here
      for (int i=0; i<arity; ++i) {
        args[i] = stack::pop<void*>(s);
      }

      stack::push(s, proto->dispatch(meth, args, arity));
    }
  }
}
