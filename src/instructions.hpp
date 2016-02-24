#pragma once

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
      fp = s;
    }

    void return_to(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "return_to" << std::endl;
#endif
      auto ret  = stack::pop<>(s);

      // reset stack to calling function
      s = fp;
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
      std::cout << "bind" << std::endl;
#endif
      auto var = (var_t::p) *(ip++);
      var->bind(stack::pop<value_t::p>(s));
    }

    void deref(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "deref" << std::endl;
#endif
      auto var = (var_t::p) *(ip++);
      stack::push(s, (int64_t) var->deref());
    }

    void enclosed(stack_t& s, stack_t& fp, int64_t* &ip) {
#ifdef _TRACE
      std::cout << "enclosed" << std::endl;
#endif
      auto fn = as<fn_t>((value_t::p) *fp);
      stack::push(s, fn->_closed_overs[*ip++]);
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
      auto f     = as<fn_t>((value_t::p) *fp);
      auto arity = *(ip++);

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
      // TODO: emit list as protocol call
      // TODO: emit list as native call
      else {
        throw std::runtime_error("Arity mismatch when calling fn");
      }
    }
  }
}
