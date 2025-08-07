
include fontcmp.f

\ H6: 16-pel text field, 10-pel letters
\ H5: 24-pel text field, 14-pel letters
\ H4: 32-pel text field, 20-pel letters  <-- default
\ H3: 48-pel text field, 30-pel letters
\ H2: 64-pel text field, 40-pel letters

counter

0 constant revision
/USED

: ﻿ ; \ ignore BOM file marker for utf-8 encoded files
include makemsgs.f

\ create fonts after messages

revision 2 /FONTS

cr .( ASCII in H4 size )
HasASCII                  \ minimum set of glyphs
usedfile glyphs.txt       \ include glyphs for this font
16 31 range               \ include fillets
H4 MakeFont
0 maketable

cr .( Numbers in H2 size)
/USED HasNumeric
H2 MakeFont
1 maketable

FONTS/

cr fhere . .( bytes of font data)
save spiflash.bin
savec myfont.h

cr .( Finished generating the fonts in )
counter swap - 0 <# # # # char . hold #s #> type .(  seconds.)

