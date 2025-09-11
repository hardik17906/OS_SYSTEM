#include <stdio.h>

int fib(int n){
    if ( n<=1 ) return n ;
    return fib(n-1) + fib(n-2) ;
}
int main(){
    int n ;
    printf("Please Enter value of n : ") ;
    scanf("%d",&n) ;
    printf("Value of fib(%d) if %d\n",n,fib(n)) ;

return 0;
}