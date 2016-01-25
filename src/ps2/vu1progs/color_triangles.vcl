
;--------------------------------------------------------------------
; color_triangles.vcl
;
; A VU1 microprogram to draw a batch of colored triangles.
; Writes the output in-place.
;--------------------------------------------------------------------

; Data offsets in the VU memory (quadword units):
#define MVP_MATRIX    0
#define SCALE_FACTORS 4
#define DRAWCALL_INFO 5
#define GIF_TAG       6
#define START_VERT    7

#vuprog VU1Prog_Color_Triangles

    ; Number of vertexes we need to process here:
    ilw.y iNumVerts, DRAWCALL_INFO(vi00)

    iaddiu iVert,    vi00, 0 ; Start vertex counter
    iaddiu iVertPtr, vi00, 1 ; Point to the first vertex (0=color qword, 1=position qword)
    iaddiu iADC,     vi00, 0 ; Load an ADC value (draw=true)

    ; Rasterizer scaling factors:
    lq fScales, SCALE_FACTORS(vi00)

    ; Model View Projection matrix:
    lq mvpMatrixRow0, MVP_MATRIX+0(vi00)
    lq mvpMatrixRow1, MVP_MATRIX+1(vi00)
    lq mvpMatrixRow2, MVP_MATRIX+2(vi00)
    lq mvpMatrixRow3, MVP_MATRIX+3(vi00)

    ; Loop for each vertex in the batch:
    vertexLoop:
        ; Load the vert (currently in object space and floating point format):
        lq Vert, START_VERT(iVertPtr)

        ; Transform the vertex by the MVP matrix:
        mul  acc,  mvpMatrixRow0, Vert[x]
        madd acc,  mvpMatrixRow1, Vert[y]
        madd acc,  mvpMatrixRow2, Vert[z]
        madd Vert, mvpMatrixRow3, Vert[w]

        ; Divide by W (perspective divide):
        div     q,    vf00[w], Vert[w]
        mul.xyz Vert, Vert,    q

        mula.xyz  acc,  fScales, vf00[w] ; Move fScales into the accumulator (acc = fScales * 1.0)
        madd.xyz  Vert, Vert,    fScales ; Multiply and add the scales (Vert = Vert * fScales + fScales)
        ftoi4.xyz Vert, Vert             ; Convert the vertex to 12:4 fixed point for the GS

        sq.xyz Vert, START_VERT(iVertPtr) ; Write the vertex back to VU memory
        isw.w  iADC, START_VERT(iVertPtr) ; Set the ADC bit to 0 to draw the vertex

        ; Increment the vertex counter and pointer and jump back to vertexLoop if not done.
        iaddiu iVert,    iVert,     1
        iaddiu iVertPtr, iVertPtr,  2
        ibne   iVert,    iNumVerts, vertexLoop
    ; END vertexLoop

    iaddiu iGIFTag, vi00, GIF_TAG ; Load the position of the GIF tag
    xgkick iGIFTag                ; and tell the VU to send that to the GS

#endvuprog
