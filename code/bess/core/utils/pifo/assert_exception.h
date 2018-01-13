#ifndef ASSERT_EXCEPTION_H_
#define ASSERT_EXCEPTION_H_

#include <exception>
#include <string>

class AssertException : public std::exception {
 public:
  AssertException(const std::string & t_error_message)
    : std::exception(),
      assert_message_(t_error_message) {}

  const char * what(void) const noexcept override { return assert_message_.c_str(); }
 private:
  std::string assert_message_;
};

# define assert_exception(expr)	if (not (expr)) { throw AssertException(\
                                                        " assert failed with expr " + std::string(__STRING(expr)) + "\n" + \
                                                        " in function " + std::string(__PRETTY_FUNCTION__) + "\n" + \
                                                        " in file " + std::string(__FILE__) + "\n" + \
                                                        " at line number " + std::to_string(__LINE__)); }



#endif  // ASSERT_EXCEPTION_H_
