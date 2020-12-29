#!/bin/bash
# Simple brutal sh-scripts chain.
if [ "$(echo $PATH | grep -o /usr/local/ps2/bin)" != "/usr/local/ps2/bin" ]
then
 echo -en "\033[36;1m There is no "/usr/local/ps2/bin" was added to \$PATH . Adding... \033[0m\n"
 export PATH=/usr/local/ps2/bin:$PATH
fi

cd ../sources
if [ ! -e glibc-2.30.tar.xz ]
then
wget https://ftp.gnu.org/gnu/glibc/glibc-2.30.tar.xz || exit -1
fi
cd ../building
tar -Jxvf ../sources/glibc-2.30.tar.xz
cd glibc-2.30
mkdir build
cd build
echo "libc_cv_forced_unwind=yes" > config.cache
echo "libc_cv_c_cleanup=yes" >> config.cache
# FPU is not optimized for nextgen PS2Linux yet. So using --without-fp
../configure mipsel-linux --prefix=/usr/local/ps2/mipsr5900el-unknown-linux-gnu --build=i686-pc-linux-gnu --host=mipsr5900el-unknown-linux-gnu --enable-shared --with-headers=/usr/local/ps2/mipsr5900el-unknown-linux-gnu/include --enable-add-ons --with-tls --with-__thread --without-fp --disable-experimental-malloc --with-pkgversion="GNU libc v2.30, nothing special" --cache-file=config.cache || exit -1

make -j 4 || exit -1
# Instead of making the dirty install with a blind hope that the new glibc will
# fully replace the old one, we make install to some temporary folder, remove
# the installed files of glibc-2.13 from the cross-compiler and inject the installed
# files by copying them from that temporary folder to the cross-compiler.
GLIBC_DIR=$(pwd)
make DESTDIR=$GLIBC_DIR/glibc_installed install || exit -1
sleep 2

rm -rf /usr/local/ps2/mipsr5900el-unknown-linux-gnu/bin/{catchsegv,gencat,getconf,getent,iconv,ldd,locale,localedef,mtrace,pcprofiledump,rpcgen,sprof,tzselect,xtrace}
rm -rf /usr/local/ps2/mipsr5900el-unknown-linux-gnu/libexec
rm -rf /usr/local/ps2/mipsr5900el-unknown-linux-gnu/sbin
rm -rf /usr/local/ps2/mipsr5900el-unknown-linux-gnu/include/{aio.h,aliases.h,alloca.h,a.out.h,argp.h,argz.h,ar.h,arpa,assert.h,bits,byteswap.h,complex.h,cpio.h,crypt.h,ctype.h,dirent.h,dlfcn.h,elf.h,endian.h,envz.h,err.h,errno.h,error.h,execinfo.h,fcntl.h,features.h,fenv.h,fmtmsg.h,fnmatch.h,fpregdef.h,fpu_control.h,fstab.h,fts.h,ftw.h,_G_config.h,gconv.h,getopt.h,glob.h,gnu,gnu-versions.h,grp.h,gshadow.h,iconv.h,ieee754.h,ifaddrs.h,inttypes.h,langinfo.h,lastlog.h,libgen.h,libintl.h,libio.h,limits.h,link.h,locale.h,malloc.h,math.h,mcheck.h,memory.h,mntent.h,monetary.h,mqueue.h,net,netash,netatalk,netax25,netdb.h,neteconet,netinet,netipx,netiucv,netpacket,netrom,netrose,nfs,nl_types.h,nss.h,obstack.h,paths.h,poll.h,printf.h,protocols,pthread.h,pty.h,pwd.h,re_comp.h,regdef.h,regex.h,regexp.h,resolv.h,rpc,rpcsvc,sched.h,scsi/scsi.h,scsi/scsi_ioctl.h,scsi/sg.h,search.h,semaphore.h,setjmp.h,sgidefs.h,sgtty.h,shadow.h,signal.h,spawn.h,stab.h,stdint.h,stdio_ext.h,stdio.h,stdlib.h,string.h,strings.h,stropts.h,sys,syscall.h,sysexits.h,syslog.h,tar.h,termio.h,termios.h,tgmath.h,thread_db.h,time.h,ttyent.h,ucontext.h,ulimit.h,unistd.h,ustat.h,utime.h,utmp.h,utmpx.h,values.h,wait.h,wchar.h,wctype.h,wordexp.h,xlocale.h}
rm -rf /usr/local/ps2/mipsr5900el-unknown-linux-gnu/lib/{crt*,gconv,gcrt1.o,ld-2.13.so,ld.so.1,libanl*,libBrokenLocale*,libbsd-compat.a,libc-2.13.so,libc.*,libcidn*,libc_nonshared.a,libcrypt*,libdl*,libg.a,libieee.a,libm-2.13.so,libm.*,libmcheck.a,libmemusage.so,libnsl*,libnss*,libpcprofile.so,libpthread*,libresolv*,librpcsvc.a,librt*,libSegFault.so,libthread_db*,libutil*,Mcrt1.o,Scrt1.o}
cp -rdf glibc_installed/usr/local/ps2/mipsr5900el-unknown-linux-gnu/* /usr/local/ps2/mipsr5900el-unknown-linux-gnu/

cd ../../../scripts
sh 6_gcc_st2_on_glibc-2.30.sh
