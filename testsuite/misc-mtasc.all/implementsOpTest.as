// implementsOpTest.as - Entry point for the implementsOp test
//
//   Copyright (C) 2007 Free Software Foundation, Inc.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
//

#include "check.as"

import implementsOp.SimpleInterface;
import implementsOp.ImplementationA;
import implementsOp.ImplementationB;
import implementsOp.BExtendingImplementation;

class implementsOpTest {
	
	var objectA:SimpleInterface;
	var objectB:SimpleInterface;
	var objectC:SimpleInterface;
	
	function implementsOpTest(testMethod:String) {
	super(testMethod);
	}
	
	function test_all():Void {
		objectA = new ImplementationA();
		objectB = new ImplementationB();
		objectC = new BExtendingImplementation();
	
		check_equals(100, objectA.doStuff(1, "foo"));
		check_equals(100, objectA.doStuff(1, "foo"));
	
		check_equals("param1 was foo", objectA.doMoreStuff("foo", 1));
	
		check_equals(200, objectB.doStuff(1, "foo"));
	
		check_equals("param2 was 1", objectB.doMoreStuff("foo", 1));
	
		check_equals(objectB.doStuff(1, "foo"), objectC.doStuff(1, "foo"));
	
		check_equals("overriding implementation", objectC.doMoreStuff("foo", 1));
	}

	static function main(mc)
	{
		var myTest = new implementsOpTest;
		myTest.test_all();

		Dejagnu.done();
	}
}

