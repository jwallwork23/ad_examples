C  =====================================================================
C
      SUBROUTINE TRANS(M,N,A,LDA,UDA,C)
C
C     Scalar arguments
      INTEGER K,M,N,LDA,UDA
C
C     Array arguments
      DOUBLE PRECISION A(LDA,UDA),C(UDA,LDA)
C
C  =====================================================================
C
C     Local scalars
      DOUBLE PRECISION TEMP
      INTEGER I,J
C
C     C := A**T
C
      DO 10 J = 1,N
          DO 20 I = 1,M
              TEMP = A(I,J)
              C(J,I) = TEMP
   20     CONTINUE
   10 CONTINUE
C
      RETURN
C
      END
