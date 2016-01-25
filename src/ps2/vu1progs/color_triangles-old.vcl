
;--------------------------------------------------------------------
; color_triangles.vcl
;
; A VU1 microprogram to draw a batch of colored triangles.
;--------------------------------------------------------------------

; Data offsets in the VU memory (quadword units):
#define MVP_MATRIX     0
#define VERT_SCALES    4
#define DEST_DATA_INFO 5
#define GIF_TAG        6
#define FIRST_VERTEX   7

#vuprog VU1Prog_Color_Triangles

    ; Input and output buffers:
    iaddiu vertexData, vi00, FIRST_VERTEX  ; First vertex follows the common draw data
    ilw.x destAdress, DEST_DATA_INFO(vi00) ; [x] element of qword has the output address while
    ilw.x kickAdress, DEST_DATA_INFO(vi00) ; [y] has the vert count, which is used below to set vertexCounter

    ; The model-view-projection matrix:
    lq mvpMatrixRow0, MVP_MATRIX+0(vi00)
    lq mvpMatrixRow1, MVP_MATRIX+1(vi00)
    lq mvpMatrixRow2, MVP_MATRIX+2(vi00)
    lq mvpMatrixRow3, MVP_MATRIX+3(vi00)

    ; A scale vector that we will use to
    ; scale the verts after projecting them.
    lq fScales, VERT_SCALES(vi00)

    ; Number of vertexes we need to process here:
    ilw.y vertexCounter, DEST_DATA_INFO(vi00)

    ; GIF tag with the primitive parameters:
    lq  gifTag, GIF_TAG(vi00)
    sqi gifTag, (destAdress++)

    ; Loop for each vertex in the buffer:
    vertexLoop:
        lqi color,  (vertexData++)
        lqi vertex, (vertexData++)

        ; Transform the vertex by the MVP matrix:
        mul  acc,    mvpMatrixRow0, vertex[x]
        madd acc,    mvpMatrixRow1, vertex[y]
        madd acc,    mvpMatrixRow2, vertex[z]
        madd vertex, mvpMatrixRow3, vertex[w]

        ; Divide by W (perspective divide):
        div q, vf00[w], vertex[w]
        mul.xyz vertex, vertex, q

        ; Scale to GS screen-space and to fixed-point:
        mula.xyz  acc,    fScales, vf00[w] ; Move fScales into the accumulator (acc = fScales * 1.0)
        madd.xyz  vertex, vertex,  fScales ; Multiply and add the scales (Vert = Vert * fScales + fScales)
        ftoi4.xyz vertex, vertex           ; Convert the vertex to 12:4 fixed point for the GS

        ; Store the transformed vertex and color value:
        sqi color, (destAdress++)
        sqi.xyz vertex, (destAdress++)

        ; Decrement the loop counter and repeat if not done yet:
        iaddi vertexCounter, vertexCounter, -1
        ibne  vertexCounter, vi00, vertexLoop
    ; END vertexLoop

    ; Dispatch the buffer to the GS rasterizer.
    xgkick kickAdress

#endvuprog
