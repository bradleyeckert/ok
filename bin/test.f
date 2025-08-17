\ tests

\. testing uops

empty \ also stops the VM

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

6 buffer: tpbuf                                 \ button/touchpad data at 1

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
: !+        swap a! !a+ a ;                     \ a n -- a'
: !@        a! @a over !a ;                     \ n addr -- n n'
: lshift    goodN for 2* next ;                 \h ~core/LSHIFT n1 -- n2
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

\ Multi-lingual messages are copied from NVM to PAD for later processing

variable language

: fontblob  ( -- faddr )                    \ fontblob begins with:
    0 nvm@[ drop  2 nvm@
    dup if  16 lshift exit  then
    4096  swap drop
;

: 'message  ( index -- NVMaddr )            \ get flash address of message, <0 if bogus
    fontblob 8 + swap
    dup >r
    over nvm@[ drop  4 nvm@  4 nvm@         ( msgs index message0 max )
    r> -  -if drop drop drop inv exit then  ( index message0 test )
    drop swap 3 * + +  nvm@[ drop           \ valid index0
    3 nvm@                                  \ address of message
    fontblob +                              \ convert to absolute NVM address
;

: msg>pad  ( index -- length )              \ copy message to pad
    'message -if dup xor exit then          \ not a valid message
    nvm@[ drop  language @                  ( language )
    begin  2 nvm@                           ( language length )
        dup 0=  if  drop dup xor exit  then \ not a valid language
        pad over for                        ( language length 'dest )
            2 nvm@ !+
        next drop
        over 0= if  swap drop exit  then    \ language found
        drop 1-
    again
;

: .message  ( index -- )                    \ simple message output
    msg>pad pad swap type
;

\ LCD cursor and screen control

host definitions
2 equ PageRounding
2 equ ButtonRounding
6 equ ButtonKerf
6 LCDparm ButtonKerf 2* - 3 / equ ButtonWidth
forth definitions

: at  ( x y -- )
    3 LCDparm!
    2 LCDparm!
;

: at@  ( -- x y )
    2 LCDparm
    3 LCDparm
;

: LCDdims  ( -- x y )
    6 LCDparm
    7 LCDparm
;

: colors  ( foreground background -- )
    1 LCDparm!
    0 LCDparm!
;

: roundEmit  ( movex movey offset -- )
    hld @                               \ 1 to 4
    1- 2* 2* 16 +  +                    ( mx my char )
    >r swap at@ r> LCDemit
    >r + swap r> +  at
;

: roundedRect  ( width height roundness -- )
    >r over over LCDfill                \ clear rectangle
    r@ if                               \ add fillets?
        r@ hld !
        r@ 6 * tuck -  >r - r>          ( W-r H-r )
        over 0   3 roundEmit            \ upper left fillet
        0 swap   1 roundEmit            \ upper right fillet
        negate 0 0 roundEmit            \ lower right fillet
        2 dup dup  roundEmit            \ lower left fillet
    then
    r> drop
;

: cls  ( -- )
    0 0 at
    LCDdims LCDfill
;

: page  ( -- )
    0 -1 colors
    0 0 at  LCDdims PageRounding roundedRect
    4 4 at
;

variable tally
variable prevbuttons

: test_buttons  ( -- )
    buttons 1 and
    prevbuttons !@ xor if
        100 100 at buttons 1 and lcd u.
    then
;

: mystuff   ( -- )
    1 tally +!
    test_buttons
;

4 cells buffer: tempAB

:noname     lcd page
            1 .message
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
1 saveblob vmblob.bin

\.
\. Interesting things to do:
\. Disassemble all: dasm
\. Benchmark: mips
\. Sanity check: counter @ u.
