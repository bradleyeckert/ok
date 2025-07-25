
include fontcmp.f

\ H6: 16-pel text field, 10-pel letters
\ H5: 24-pel text field, 14-pel letters
\ H4: 32-pel text field, 20-pel letters  <-- default
\ H3: 48-pel text field, 30-pel letters
\ H2: 64-pel text field, 40-pel letters

counter

0 constant revision

revision 2 /FONTS

cr .( ASCII in H4 size )
/msg HasASCII           \ minimum set of glyphs
MSGfile glyphs.txt      \ include glyphs for this text
H4 MakeFont
0 maketable

cr .( Numbers in H2 size)
/msg HasNumeric
H2 MakeFont
1 maketable

cr fhere . .( bytes of data total)
save myfont.bin
savec myfont.h

cr .( Finished generating the fonts in )
counter swap - 0 <# # # # char . hold #s #> type .(  seconds.)

