// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/module.h>

static int __init test_sscanf(void)
{
   struct {
      int v, y, m, d;
      char r, u;
   } s = { };
   int n;

   n = sscanf("0170EC20031217", "%4x%c%c%4d%2d%2d",
         &s.v, &s.r, &s.u, &s.y, &s.m, &s.d);

   pr_info("sscanf %d %x\n", n, s.v);

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
