// 2004-06-25  Paolo Carlini  <pcarlini@suse.de>

// Copyright (C) 2004 Free Software Foundation, Inc.
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

// 25.2.5 [lib.alg.fill] Fill

#include <list>
#include <algorithm>
#include <testsuite_hooks.h>

class num
{
  int stored;

public:
  num(int init = 0)
  : stored(init)
  { }

  operator int() const
  { return stored; }
};

// fill_n
void test01()
{
  bool test __attribute__((unused)) = true;
  using namespace std;

  const int val = 3;

  const int V[] = { val, val, val, val, val, val, val, val, val };
  const list<int>::size_type N = sizeof(V) / sizeof(int);

  list<int> coll(N);
  fill_n(coll.begin(), coll.size(), val);
  VERIFY( equal(coll.begin(), coll.end(), V) );

  list<num> coll2(N);
  fill_n(coll2.begin(), coll2.size(), val);
  VERIFY( equal(coll2.begin(), coll2.end(), V) );
}

#if !__GXX_WEAK__ && _MT_ALLOCATOR_H
// Explicitly instantiate for systems with no COMDAT or weak support.
template class __gnu_cxx::__mt_alloc<std::_List_node<int> >;
template class __gnu_cxx::__mt_alloc<std::_List_node<num> >;
#endif

int main()
{
  test01();
  return 0;
}
