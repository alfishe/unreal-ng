# vcd-writer

Port PyVCD package from python to C++

Visit [PyVCD on GitHub](https://github.com/SanDisk-Open-Source/pyvcd/).

It writes Value Change Dump (VCD) files as specified in IEEE 1364-2005.

Quick Start
-----------
```C++
	#include "vcd_writer.h"
	using namespace vcd;
	
	HeadPtr head = makeVCDHeader(TimeScale::ONE, TimeScaleUnit::ns, utils::now());
	VCDWriter writer{"dump.vcd", head};
	VarPtr counter_var = writer.register_var("a.b.c", "counter", VariableType::integer, 8);
	for (int timestamp = 0; timestamp < 5; ++timestamp)
	{
		char value = 10 + timestamp * 2; 
		writer.change(counter_var, timestamp, utils::format("%b", value));
	}
```
Output:

	$date 2017-11-29 13:14:25 $end
	$timescale 1 ns $end
	$scope module a $end
	$scope module b $end
	$scope module c $end
	$var integer 8 0 counter $end
	$upscope $end
	$upscope $end
	$upscope $end
	$enddefinitions $end
	#0
	$dumpvars
	b00001010 0
	$end
	#1
	b00001100 0
	#2
	b00001110 0
	#3
	b00010000 0
	#4
	b00010010 0
