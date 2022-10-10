#include <setjmp.h>// 这是一份学习c语言的jump的代码
#include <stdio.h>
#include <stdlib.h>
static jmp_buf buf;
#define COUNT(M, q) M *M //定义有参宏
int x = 6;

int main()
{

    char* abc="bbb""aaa";
    printf("%s",abc);
    printf("COUNT = %d\n", COUNT(x + 1,1)); // 输出结果： COUNT = 13
    printf("COUNT = %d\n", COUNT(++x,1));   // 输出结果： COUNT = 56
    volatile int b;
    b = 3;

    if (setjmp(buf) != 0)
    {
        printf("%d ", b);
        exit(0);
    }
    b = 5;
    longjmp(buf, 1);
}

// 这个代码里运行步骤

// 1.先执行setjmp，因为是第一次设置跳转点，返回值是0，不执行if语句块里的语句，

//     2.然后执行b = 5，b的值就是5了

//     3.再执行longjmp跳转之后， 最后再执行setjmp， 这时setjmp会返回1（也就是longjmp的第二个参数指定的值），就会执行if语句块里的语句-- --打印之后终止程序，这时b的值是5，就会打印出5来