c---------------------------------------------------------------------
c---------------------------------------------------------------------

      subroutine blts ( ldmx, ldmy, ldmz,
     >                  nx, ny, nz, k,
     >                  omega,
     >                  v, 
     >                  ldz, ldy, ldx, d,
     >                  ist, iend, jst, jend,
     >                  nx0, ny0 )

c---------------------------------------------------------------------
c---------------------------------------------------------------------

c---------------------------------------------------------------------
c
c   compute the regular-sparse, block lower triangular solution:
c
c                     v <-- ( L-inv ) * v
c
c---------------------------------------------------------------------

      implicit none

c---------------------------------------------------------------------
c  input parameters
c---------------------------------------------------------------------
      integer ldmx, ldmy, ldmz
      integer nx, ny, nz
      integer k
      double precision  omega
c---------------------------------------------------------------------
c   To improve cache performance, second two dimensions padded by 1 
c   for even number sizes only.  Only needed in v.
c---------------------------------------------------------------------
      double precision  v( 5, ldmx/2*2+1, ldmy/2*2+1, *),
     >        ldz( 5, 5, ldmx/2*2+1, ldmy),
     >        ldy( 5, 5, ldmx/2*2+1, ldmy),
     >        ldx( 5, 5, ldmx/2*2+1, ldmy),
     >        d( 5, 5, ldmx/2*2+1, ldmy)
      integer ist, iend
      integer jst, jend
      integer nx0, ny0

c---------------------------------------------------------------------
c  local variables
c---------------------------------------------------------------------
      integer i, j, m
      double precision  tmp, tmp1
      double precision  tmat(5,5), tv(5)

c---------------------------------------------------------------------
c  Thread synchronization for pipeline operation
c---------------------------------------------------------------------
      include 'npbparams.h'

      integer isync(0:isiz2), mthreadnum, iam
      common /threadinfo1/ isync
      common /threadinfo2/ mthreadnum, iam
!$omp threadprivate(/threadinfo2/)

      integer neigh


      if (iam .gt. 0 .and. iam .le. mthreadnum) then
         neigh = iam - 1
         do while (isync(neigh) .eq. 0)
!$omp flush(isync)
         end do
         isync(neigh) = 0
!$omp flush(isync,v)
      endif


!$omp do schedule(static)
      do j = jst, jend
         do i = ist, iend
            do m = 1, 5

                  tv( m ) =  v( m, i, j, k )
     >    - omega * (  ldz( m, 1, i, j ) * v( 1, i, j, k-1 )
     >               + ldz( m, 2, i, j ) * v( 2, i, j, k-1 )
     >               + ldz( m, 3, i, j ) * v( 3, i, j, k-1 )
     >               + ldz( m, 4, i, j ) * v( 4, i, j, k-1 )
     >               + ldz( m, 5, i, j ) * v( 5, i, j, k-1 )  )

            end do


            do m = 1, 5

                  tv( m ) =  tv( m )
     > - omega * ( ldy( m, 1, i, j ) * v( 1, i, j-1, k )
     >           + ldx( m, 1, i, j ) * v( 1, i-1, j, k )
     >           + ldy( m, 2, i, j ) * v( 2, i, j-1, k )
     >           + ldx( m, 2, i, j ) * v( 2, i-1, j, k )
     >           + ldy( m, 3, i, j ) * v( 3, i, j-1, k )
     >           + ldx( m, 3, i, j ) * v( 3, i-1, j, k )
     >           + ldy( m, 4, i, j ) * v( 4, i, j-1, k )
     >           + ldx( m, 4, i, j ) * v( 4, i-1, j, k )
     >           + ldy( m, 5, i, j ) * v( 5, i, j-1, k )
     >           + ldx( m, 5, i, j ) * v( 5, i-1, j, k ) )

            end do
       
c---------------------------------------------------------------------
c   diagonal block inversion
c
c   forward elimination
c---------------------------------------------------------------------
            do m = 1, 5
               tmat( m, 1 ) = d( m, 1, i, j )
               tmat( m, 2 ) = d( m, 2, i, j )
               tmat( m, 3 ) = d( m, 3, i, j )
               tmat( m, 4 ) = d( m, 4, i, j )
               tmat( m, 5 ) = d( m, 5, i, j )
            end do

            tmp1 = 1.0d+00 / tmat( 1, 1 )
            tmp = tmp1 * tmat( 2, 1 )
            tmat( 2, 2 ) =  tmat( 2, 2 )
     >           - tmp * tmat( 1, 2 )
            tmat( 2, 3 ) =  tmat( 2, 3 )
     >           - tmp * tmat( 1, 3 )
            tmat( 2, 4 ) =  tmat( 2, 4 )
     >           - tmp * tmat( 1, 4 )
            tmat( 2, 5 ) =  tmat( 2, 5 )
     >           - tmp * tmat( 1, 5 )
            tv( 2 ) = tv( 2 )
     >        - tv( 1 ) * tmp

            tmp = tmp1 * tmat( 3, 1 )
            tmat( 3, 2 ) =  tmat( 3, 2 )
     >           - tmp * tmat( 1, 2 )
            tmat( 3, 3 ) =  tmat( 3, 3 )
     >           - tmp * tmat( 1, 3 )
            tmat( 3, 4 ) =  tmat( 3, 4 )
     >           - tmp * tmat( 1, 4 )
            tmat( 3, 5 ) =  tmat( 3, 5 )
     >           - tmp * tmat( 1, 5 )
            tv( 3 ) = tv( 3 )
     >        - tv( 1 ) * tmp

            tmp = tmp1 * tmat( 4, 1 )
            tmat( 4, 2 ) =  tmat( 4, 2 )
     >           - tmp * tmat( 1, 2 )
            tmat( 4, 3 ) =  tmat( 4, 3 )
     >           - tmp * tmat( 1, 3 )
            tmat( 4, 4 ) =  tmat( 4, 4 )
     >           - tmp * tmat( 1, 4 )
            tmat( 4, 5 ) =  tmat( 4, 5 )
     >           - tmp * tmat( 1, 5 )
            tv( 4 ) = tv( 4 )
     >        - tv( 1 ) * tmp

            tmp = tmp1 * tmat( 5, 1 )
            tmat( 5, 2 ) =  tmat( 5, 2 )
     >           - tmp * tmat( 1, 2 )
            tmat( 5, 3 ) =  tmat( 5, 3 )
     >           - tmp * tmat( 1, 3 )
            tmat( 5, 4 ) =  tmat( 5, 4 )
     >           - tmp * tmat( 1, 4 )
            tmat( 5, 5 ) =  tmat( 5, 5 )
     >           - tmp * tmat( 1, 5 )
            tv( 5 ) = tv( 5 )
     >        - tv( 1 ) * tmp



            tmp1 = 1.0d+00 / tmat( 2, 2 )
            tmp = tmp1 * tmat( 3, 2 )
            tmat( 3, 3 ) =  tmat( 3, 3 )
     >           - tmp * tmat( 2, 3 )
            tmat( 3, 4 ) =  tmat( 3, 4 )
     >           - tmp * tmat( 2, 4 )
            tmat( 3, 5 ) =  tmat( 3, 5 )
     >           - tmp * tmat( 2, 5 )
            tv( 3 ) = tv( 3 )
     >        - tv( 2 ) * tmp

            tmp = tmp1 * tmat( 4, 2 )
            tmat( 4, 3 ) =  tmat( 4, 3 )
     >           - tmp * tmat( 2, 3 )
            tmat( 4, 4 ) =  tmat( 4, 4 )
     >           - tmp * tmat( 2, 4 )
            tmat( 4, 5 ) =  tmat( 4, 5 )
     >           - tmp * tmat( 2, 5 )
            tv( 4 ) = tv( 4 )
     >        - tv( 2 ) * tmp

            tmp = tmp1 * tmat( 5, 2 )
            tmat( 5, 3 ) =  tmat( 5, 3 )
     >           - tmp * tmat( 2, 3 )
            tmat( 5, 4 ) =  tmat( 5, 4 )
     >           - tmp * tmat( 2, 4 )
            tmat( 5, 5 ) =  tmat( 5, 5 )
     >           - tmp * tmat( 2, 5 )
            tv( 5 ) = tv( 5 )
     >        - tv( 2 ) * tmp



            tmp1 = 1.0d+00 / tmat( 3, 3 )
            tmp = tmp1 * tmat( 4, 3 )
            tmat( 4, 4 ) =  tmat( 4, 4 )
     >           - tmp * tmat( 3, 4 )
            tmat( 4, 5 ) =  tmat( 4, 5 )
     >           - tmp * tmat( 3, 5 )
            tv( 4 ) = tv( 4 )
     >        - tv( 3 ) * tmp

            tmp = tmp1 * tmat( 5, 3 )
            tmat( 5, 4 ) =  tmat( 5, 4 )
     >           - tmp * tmat( 3, 4 )
            tmat( 5, 5 ) =  tmat( 5, 5 )
     >           - tmp * tmat( 3, 5 )
            tv( 5 ) = tv( 5 )
     >        - tv( 3 ) * tmp



            tmp1 = 1.0d+00 / tmat( 4, 4 )
            tmp = tmp1 * tmat( 5, 4 )
            tmat( 5, 5 ) =  tmat( 5, 5 )
     >           - tmp * tmat( 4, 5 )
            tv( 5 ) = tv( 5 )
     >        - tv( 4 ) * tmp

c---------------------------------------------------------------------
c   back substitution
c---------------------------------------------------------------------
            v( 5, i, j, k ) = tv( 5 )
     >                      / tmat( 5, 5 )

            tv( 4 ) = tv( 4 )
     >           - tmat( 4, 5 ) * v( 5, i, j, k )
            v( 4, i, j, k ) = tv( 4 )
     >                      / tmat( 4, 4 )

            tv( 3 ) = tv( 3 )
     >           - tmat( 3, 4 ) * v( 4, i, j, k )
     >           - tmat( 3, 5 ) * v( 5, i, j, k )
            v( 3, i, j, k ) = tv( 3 )
     >                      / tmat( 3, 3 )

            tv( 2 ) = tv( 2 )
     >           - tmat( 2, 3 ) * v( 3, i, j, k )
     >           - tmat( 2, 4 ) * v( 4, i, j, k )
     >           - tmat( 2, 5 ) * v( 5, i, j, k )
            v( 2, i, j, k ) = tv( 2 )
     >                      / tmat( 2, 2 )

            tv( 1 ) = tv( 1 )
     >           - tmat( 1, 2 ) * v( 2, i, j, k )
     >           - tmat( 1, 3 ) * v( 3, i, j, k )
     >           - tmat( 1, 4 ) * v( 4, i, j, k )
     >           - tmat( 1, 5 ) * v( 5, i, j, k )
            v( 1, i, j, k ) = tv( 1 )
     >                      / tmat( 1, 1 )


        enddo
      enddo
!$omp end do nowait

      if (iam .lt. mthreadnum) then
!$omp flush(isync,v)
         do while (isync(iam) .eq. 1)
!$omp flush(isync)
         end do
         isync(iam) = 1
!$omp flush(isync)
      endif


      return
      end


