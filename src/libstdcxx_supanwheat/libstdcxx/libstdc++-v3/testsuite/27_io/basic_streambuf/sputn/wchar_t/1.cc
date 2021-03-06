// 1999-10-11 bkoz

// Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004
// Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

// 27.5.2 template class basic_streambuf

#include <streambuf>
#include <cwchar>
#include <testsuite_hooks.h>

class testbuf : public std::wstreambuf
{
public:

  // Typedefs:
  typedef std::wstreambuf::traits_type traits_type;
  typedef std::wstreambuf::char_type char_type;

  testbuf(): std::wstreambuf() 
  { }

  bool
  check_pointers()
  { 
    bool test __attribute__((unused)) = true;
    VERIFY( this->eback() == NULL );
    VERIFY( this->gptr() == NULL );
    VERIFY( this->egptr() == NULL );
    VERIFY( this->pbase() == NULL );
    VERIFY( this->pptr() == NULL );
    VERIFY( this->epptr() == NULL );
    return test;
  }

  int_type 
  pub_uflow() 
  { return (this->uflow()); }

  int_type 
  pub_overflow(int_type __c = traits_type::eof()) 
  { return (this->overflow(__c)); }

  int_type 
  pub_pbackfail(int_type __c) 
  { return (this->pbackfail(__c)); }

  void 
  pub_setg(wchar_t* beg, wchar_t* cur, wchar_t* end) 
  { this->setg(beg, cur, end); }

  void 
  pub_setp(wchar_t* beg, wchar_t* end)
  { this->setp(beg, end); }

protected:
  int_type 
  underflow() 
  { 
    int_type __retval = traits_type::eof();
    if (this->gptr() < this->egptr())
      __retval = traits_type::not_eof(0); 
    return __retval;
  }
};

void test01()
{
  typedef testbuf::traits_type traits_type;
  typedef testbuf::int_type int_type;

  bool test __attribute__((unused)) = true;
  testbuf buf01;

  // sputn/xsputn
  const wchar_t* lit01 = L"isotope 217: the unstable molecule on thrill jockey";
  const int i02 = std::wcslen(lit01);
  wchar_t lit02[i02];
  std::wcscpy(lit02, lit01);

  wchar_t carray[i02 + 1];
  std::wmemset(carray, 0, i02 + 1);

  buf01.pub_setp(carray, (carray + i02));
  buf01.sputn(lit02, 0);
  VERIFY( carray[0] == 0 );
  VERIFY( lit02[0] == L'i' );
  buf01.sputn(lit02, 1);
  VERIFY( lit02[0] == carray[0] );
  VERIFY( lit02[1] == L's' );
  VERIFY( carray[1] == 0 );
  buf01.sputn(lit02 + 1, 10);
  VERIFY( std::memcmp(lit02, carray, 10) == 0 );
  buf01.sputn(lit02 + 11, 20);
  VERIFY( std::memcmp(lit02, carray, 30) == 0 );
}

int main() 
{
  test01();
  return 0;
}
