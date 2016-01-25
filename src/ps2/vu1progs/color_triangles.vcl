
#define ProjMat          0
#define Scale            4
#define DEST_DATA_INFO   5
#define GIFTag           6
#define StartVert        7

;#define NumVerts         3

#vuprog VU1Prog_Color_Triangles

    ; Number of vertexes we need to process here:
    ilw.y iNumVerts, DEST_DATA_INFO(vi00)

    iaddiu      iVert, vi00, 0              ; Start vertex counter
    iaddiu      iVertPtr, vi00, 1           ; Point to the first vert (0=color qword, 1=position qword)
;    iaddiu      iNumVerts, vi00, NumVerts   ; Load the loop end condition
    iaddiu      iADC, vi00, 0               ; Load an ADC value (draw = true)

    lq          fScales, Scale(vi00)        ; A scale vector that we will add
                                            ; to scale the vertices after projection

    lq          mvpMatrixRow0, ProjMat+0(vi00)
    lq          mvpMatrixRow1, ProjMat+1(vi00)
    lq          mvpMatrixRow2, ProjMat+2(vi00)
    lq          mvpMatrixRow3, ProjMat+3(vi00)

loop:                                       ; For each vertex
    lq          Vert, StartVert(iVertPtr)   ; Load the vector (currently in object space
                                            ; and floating point format)

    ; Transform the vertex by the MVP matrix:
    mul  acc,    mvpMatrixRow0, Vert[x]
    madd acc,    mvpMatrixRow1, Vert[y]
    madd acc,    mvpMatrixRow2, Vert[z]
    madd Vert,   mvpMatrixRow3, Vert[w]

    div         q,    vf00[w], Vert[w]      ; Work out 1.0f / w
    mul.xyz     Vert, Vert, q               ; Project the vertex by dividing by W

    mula.xyz    acc, fScales, vf00[w]       ; Move fScales into the accumulator (acc = fScales * 1.0)
    madd.xyz    Vert, Vert, fScales         ; Multiply and add the scales (Vert = Vert * fScales + fScales)
    ftoi4.xyz   Vert, Vert                  ; Convert the vertex to 12:4 fixed point for the GS

    sq.xyz      Vert, StartVert(iVertPtr)   ; Write the vertex back to VU memory
    isw.w       iADC, StartVert(iVertPtr)   ; Set the ADC bit to 0 to draw vertex

    iaddiu      iVert, iVert, 1             ; Increment the vertex counter
    iaddiu      iVertPtr, iVertPtr, 2       ; Increment the vertex pointer
    ibne        iVert, iNumVerts, loop      ; Branch back to "loop" label if
                                            ; iVert and iNumVerts are not equal

    iaddiu      iGIFTag, vi00, GIFTag       ; Load the position of the giftag
    xgkick      iGIFTag                     ; and tell the PS2 to send that to the GS

#endvuprog
