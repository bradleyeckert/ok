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

here ' con-emit , ' con-cr , equ con-outputs
variable outputs

: console   con-outputs outputs ! ;             \ -- | direct output to console
: out-exec  outputs @ + @ >r ;
: emit      0 out-exec ;                        \h ~core/EMIT c --
: cr        1 out-exec ;                        \h ~core/CR --

\ Define numeric output lexicon

: -         1 swap invert + + ;                 \h ~core/Minus n1 n2 -- n3
: or        inv swap inv and inv ;              \h ~core/OR x1 x2 -- x3
: rot       >r swap r> swap ;                   \h ~core/ROT x1 x2 x3 -- x2 x3 x1
: dnegate   inv swap inv 1 + swap cy + ;        \h ~double/DNEGATE d -- -d
: dabs      -if dnegate then ;                  \h ~double/DABS d -- ud
: 0=        if 0 exit then -1 ;                 \h ~core/ZeroEqual x -- flag
: s>d       dup [ ;                             \h ~core/StoD n -- d
: 0<        -if drop -1 exit then drop 0 ;      \h ~core/ZeroLess x -- flag
: @+        a! @a+ a swap ;                     \h  a -- a+1 n
: 1+        1 + ;                               \h ~core/OnePlus n1 -- n2
: 1-        -1 + ;                              \h ~core/OneMinus n1 -- n2
: goodN     1- -if  drop r> drop exit then 1+ ; \h  n1 -- (n1<=0?) | n1
: goodAN    1- -if 2drop r> drop exit then 1+ ; \h  n1 n2 -- (n2<=0?) | n1 n2
: type      goodAN for @+ emit next drop ;      \h ~core/TYPE a n --

variable hld                                    \h  -- a \ pointer for numeric output
32 equ bl                                       \h ~core/BL -- char
pad |pad| + equ numbuf
0 equ base                                      \h ~core/BASE -- a

: space     bl emit ;                           \h ~core/SPACE --
: spaces    goodN for space next ;              \h ~core/SPACES n --
: digit     -10 + -if -7 + then [char] A + ;    \h  u -- char
: <#        numbuf  hld ! ;                     \h ~core/num-start --
: hold      hld a! @a -1 + dup !a ! ;           \h ~core/HOLD char --
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

: negate    1 swap invert + ;                   \h ~core/NEGATE n1 -- n2
: abs       -if negate then ;                   \h ~core/ABS n -- u
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

variable counter

: mystuff   1 counter +! ;

2 cells buffer: tempAB

:noname     console
            begin
                bcisync
                a b tempAB a! !a+ !a            \ save A and B registers
                mystuff
                tempAB a! @a+ b! @a a!          \ restore A and B registers
            again
; resolves coldboot

reload \ synchronize code and text images to target
\ executing words without reload will crash

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
17 port!

