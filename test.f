\ tests

\. testing uops

empty

\ The VM may be running, so it may stomp on cy. cy not tested.

t{ 1 nop -> 1 }t
t{ 1 dup -> 1 1 }t
t{ 3 5 drop -> 3 }t
t{ 1 inv -> -2 }t
t{ 1 2 over -> 1 2 1 }t
t{ 100 a! a -> 100 }t
t{ 3 5 +   -> 8 }t
t{ 3 5 xor -> 6 }t
t{ 3 5 and -> 1 }t
t{ 3 5 swap -> 5 3 }t
t{ 3 2* -> 6 }t
t{ 3 dup -> 3 3 }t
t{ 0 -> 0 }t
t{ 100 a! 1000 !a -> }t
t{ 100 a! @a -> 1000 }t
t{ 0 a! @a -> 10 }t

\ Forth definitions

later coldboot

\ Text output needs some indirection

: con-emit  semit ;
: con-cr    13 semit 10 semit ;
: lcd-emit  LCDemit ;
: lcd-cr    10 LCDemit ;

here ' con-emit , ' con-cr , equ con-outputs
here ' lcd-emit , ' lcd-cr , equ lcd-outputs
variable outputs

: console   con-outputs outputs ! ;             \ -- | direct output to console
: lcd       lcd-outputs outputs ! ;             \ -- | direct output to LCD
: out-exec  outputs @ + @ >r ;
: emit      0 out-exec ;                        \h ~core/EMIT c --
: cr        1 out-exec ;                        \h ~core/CR --

\ Define numeric output lexicon

: -         invert 1 + + ;                      \h ~core/Minus n1 n2 -- n3
: or        inv swap inv and inv ;              \h ~core/OR x1 x2 -- x3
: rot       >r swap r> swap ;                   \h ~core/ROT x1 x2 x3 -- x2 x3 x1
: dnegate   inv swap inv 1 + swap cy + ;        \h ~double/DNEGATE d -- -d
: dabs      -if dnegate then ;                  \h ~double/DABS d -- ud
: 0=        if 0 exit then -1 ;                 \h ~core/ZeroEqual x -- flag
: s>d       dup [ ;                             \h ~core/StoD n -- d
: 0<        -if dup xor inv exit then dup xor ; \h ~core/ZeroLess x -- flag
: @+        a! @a+ a swap ;                     \h  a -- a+1 n
: 1+        1 + ;                               \h ~core/OnePlus n1 -- n2
: 1-        -1 + ;                              \h ~core/OneMinus n1 -- n2
: goodN     1- -if  drop r> drop exit then 1+ ; \h  n1 -- (n1<=0?) | n1
: goodAN    1- -if 2drop r> drop exit then 1+ ; \h  n1 n2 -- (n2<=0?) | n1 n2
: type      goodAN for @+ emit next drop ;      \h ~core/TYPE a n --

variable hld                                    \h  -- a \ pointer for numeric output
32 equ bl                                       \h ~core/BL -- char
256 equ |pad|
|pad| buffer: pad
|pad| pad + equ numbuf
0 equ base                                      \h ~core/BASE -- a

: space     bl emit ;                           \h ~core/SPACE --
: spaces    goodN for space next ;              \h ~core/SPACES n --
: digit     -10 + -if -7 + then [char] A + ;    \h  u -- char
: <#        numbuf  hld ! ;                     \h ~core/num-start --
: hold      hld a! @a 1- dup !a ! ;             \h ~core/HOLD char --
: #         base @ mu/mod rot digit hold ;      \h ~core/num ud1 -- ud2
: #s        begin # 2dup or 0= until ;          \h ~core/numS ud1 -- ud2
: sign      0< if [char] - hold then ;          \h ~core/SIGN n --
: #>        2drop hld @ numbuf  over - ;        \h ~core/num-end ud -- c-addr u
: s.r       over - spaces type ;                \h  a u width --
: d.r       >r dup >r dabs                      \h ~double/DDotR d width --
            <# #s r> sign #> r> s.r  ;
: u.r       0 swap d.r ;                        \h ~core/UDotR u width --
: .r        >r s>d r> d.r ;                     \h ~core/DotR n width --
: d.        0 d.r space ;                       \h ~double/Dd d --
: u.        0 d. ;                              \h ~core/Ud u --

.( Numeric output has been defined in ) cp -1 + . \. instructions.

: negate    invert 1 + ;                        \h ~core/NEGATE n1 -- n2
: abs       -if negate then ;                   \h ~core/ABS n -- u
: *         um* drop ;                          \h ~core/Times n1 n2 -- n3
: m*        2dup xor >r abs swap abs um*        \h ~core/MTimes n1 n2 -- d
            r> 0< if dnegate then ;

: m/mod
    dup 0< dup >r
    if negate  >r
       dnegate r>
    then >r dup 0<
    if r@ +
    then r> um/mod
    r> if
       swap negate swap
    then
;

: */        >r m* r> m/mod swap drop ;

: d2/       2/ swap 2/c swap ;                  \h ~double/DTwoDiv d1 -- d2
: +!        a! @a + !a ;                        \h ~core/PlusStore n a --
: noop      ;                                   \ --
: off       a! 0 !a ;                           \ a --
: on        a! -1 !a ;                          \ a --
: star      42 emit ;
: stars     goodN for star next ;
: euros     0 <# # # [char] . hold              \ cents --
                  #s [char] € hold #> type ;    \ demonstrate wide char (20AC)

tp equ table  100 , 1000 , 10000 ,
," こんにちは世界" : hi  literal @+ type ;
: yo        ," ok!" @+ type ;

\ Multi-lingual messages are copied from NVM to PAD for processing

variable language

: 'message  ( index -- NVMaddr )        \ get flash address of message
    0 nvm@[ drop   3 *
    4 nvm@ +  nvm@[ drop
    3 nvm@
;

\ : SAYS  ( ca0 -- ca1 )
\    DUP LANG
\    BEGIN  DUP WHILE  1- >R     ( ca0 ca )
\       COUNTC +                          \ skip to next string
\       DUP C@C 0= IF                     \ oops, hit the terminator
\          R> DROP DROP EXIT              \ revert to 1st language
\       THEN
\    R> REPEAT  DROP NIP
\ ;

variable tally

: mystuff   1 tally +! ;

4 cells buffer: tempAB

:noname     console
            begin
                bcisync
                a b tempAB a! !a+ y@ !a+ x@ !a+ !a+    \ save B, Y, X, and A registers
                mystuff
                tempAB a! @a+ b! @a+ y! @a+ x! @a a!   \ restore registers
            again
; resolves coldboot

reload \ synchronize code and text images to target

console \ initialize console output

\. testing Forth

t{ 5 negate -> -5 }t
t{ 5 3 - -> 2 }t
t{ 1 2 3 rot -> 2 3 1 }t
t{ 0 0= -> -1 }t
t{ 1 0= -> -0 }t
t{ table @ -> 100 }t
t{ table 1 + @ -> 1000 }t
t{ table 2 + @ -> 10000 }t

\ verbose_bci verbose!
5 port!

\. defining some words that only execute on the STM32
hex

40004800 peripheral USART3_BASE

000 register USART_CR1 \ USART Control register 1

decimal
: com3cr  USART3_BASE USART_CR1 @b+ ;

reload \ synchronize again before leaving

\.
\. Interesting things to do:
\. Disassemble all: dasm
\. Benchmark: mips
\. Sanity check: counter @ u.
