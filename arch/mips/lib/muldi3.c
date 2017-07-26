/* Code is copied from libgcc. */

#define W_TYPE_SIZE (4 * BITS_PER_UNIT)
#define Wtype	SItype
#define UWtype	USItype
#define HWtype	SItype
#define UHWtype	USItype
#define DWtype	DItype
#define UDWtype	UDItype

#if (__GNUC__ == 4) && (__GNUC_MINOR__ < 9) /* TBD: Not working with newer compiler. */
#define umul_ppmm(w1, w0, u, v) \
__asm__ ("multu %2,%3"						\
   : "=l" ((USItype) (w0)),					\
     "=h" ((USItype) (w1))					\
   : "d" ((USItype) (u)),					\
     "d" ((USItype) (v)))
#else
#define umul_ppmm(w1, w0, u, v) \
__asm__ ("multu %2,%3\n"					\
	"mfhi %1\n"						\
   : "=l" ((USItype) (w0)),					\
     "=d" ((USItype) (w1))					\
   : "d" ((USItype) (u)),					\
     "d" ((USItype) (v)))
#endif

#define __umulsidi3(u, v) \
  ({DWunion __w;							\
    umul_ppmm (__w.s.high, __w.s.low, u, v);				\
    __w.ll; })

typedef 	 int SItype	__attribute__ ((mode (SI)));
typedef unsigned int USItype	__attribute__ ((mode (SI)));
typedef		 int DItype	__attribute__ ((mode (DI)));
typedef unsigned int UDItype	__attribute__ ((mode (DI)));

#ifdef CONFIG_CPU_BIG_ENDIAN
  struct DWstruct {Wtype high, low;};
#else
  struct DWstruct {Wtype low, high;};
#endif

typedef union
{
  struct DWstruct s;
  DWtype ll;
} DWunion;

DWtype
__muldi3 (DWtype u, DWtype v)
{
  const DWunion uu = {.ll = u};
  const DWunion vv = {.ll = v};
  DWunion w = {.ll = __umulsidi3 (uu.s.low, vv.s.low)};

  w.s.high += ((UWtype) uu.s.low * (UWtype) vv.s.high
	       + (UWtype) uu.s.high * (UWtype) vv.s.low);

  return w.ll;
}
