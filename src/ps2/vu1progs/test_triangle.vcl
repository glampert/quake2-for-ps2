
;;
;; test_triangle.vcl
;;
;; A VU1 microprogram to draw a single triangle.
;;

.syntax new
.name vu1Triangle
.vu
.init_vf_all
.init_vi_all

--enter
--endenter

    ; The model-view-projection matrix:
    lq mvpMatrixRow0, 0(vi00)
    lq mvpMatrixRow1, 1(vi00)
    lq mvpMatrixRow2, 2(vi00)
    lq mvpMatrixRow3, 3(vi00)

    ; A scale vector that we will use to
    ; scale the verts after projecting them.
    lq fScales, 4(vi00)

    iaddiu vertexData, vi00, 7
    iaddiu destAdress, vi00, 20
    iaddiu kickAdress, vi00, 20

    lq  gifTag, 5(vi00)
    sqi gifTag, (destAdress++)

    lq  color, 6(vi00)
    sqi color, (destAdress++)

    iaddiu vertexCounter, vi00, 3

    ;;
    ;; Loop for each of the 3 vertexes in the triangle:
    ;;
    vertexLoop:
        lqi vertex, (vertexData++)

        ; Transform each vertex by the MVP:
        mul  acc,    mvpMatrixRow0, vertex[x]
        madd acc,    mvpMatrixRow1, vertex[y]
        madd acc,    mvpMatrixRow2, vertex[z]
        madd vertex, mvpMatrixRow3, vertex[w]

        ; Divide by the W (perspective divide):
        div q, vf00[w], vertex[w]
        mul.xyz vertex, vertex, q

        ; Scale to GS screen-space and to fixed-point:
        mula.xyz  acc,    fScales, vf00[w] ; Move fScales into the accumulator (acc = fScales * 1.0)
        madd.xyz  vertex, vertex,  fScales ; Multiply and add the scales (Vert = Vert * fScales + fScales)
        ftoi4.xyz vertex, vertex           ; Convert the vertex to 12:4 fixed point for the GS

        ; Store the transformed vertex:
        sqi vertex, (destAdress++)

        ; Increment the loop counter and repeat
        ; if not done with the triangle yet:
        iaddi vertexCounter, vertexCounter, -1
        ibne  vertexCounter, vi00, vertexLoop
    ; END vertexLoop

    ; Dispatch the triangle to the GS rasterizer.
    xgkick kickAdress

--exit
--endexit

