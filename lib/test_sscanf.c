// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/kernel.h>

static int __init test_sscanf(void)
{
#if 1
	int year, month, day, hour, minute, second;
	int r;

	r = sscanf("20190523123456", "%4d%2d%2d%2d%2d%2d",
		&year, &month, &day,
		&hour, &minute, &second);

	pr_info("%s: %d %04d-%02d-%2d %02d:%02d:%02d\n",
		__func__, r,
		year, month, day,
		hour, minute, second);
#else
   struct {
      int v, y, m, d;
      char r, u;
   } s = { };
   int n;

   n = sscanf("0170EC20031217", "%4x", &s.v);

   pr_info("sscanf %d %x\n", n, s.v);
#endif

   return 0;
}

static int __init test_sscanf_init(void)
{
   return test_sscanf();
}

static void __exit test_sscanf_exit(void)
{
}

module_init(test_sscanf_init);
module_exit(test_sscanf_exit);

MODULE_LICENSE("GPL");
