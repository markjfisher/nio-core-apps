/* clib2. Default Shell STACK is 4096; FLS/FHOST frames overflow that
 * (Guru #80000006 CHK). Same request as fujinet-nio-exchange. */
long __stack = 16384;
