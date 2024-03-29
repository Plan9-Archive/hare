c---------------------------------------------------------------------
c---------------------------------------------------------------------

      subroutine ssor(niter)

c---------------------------------------------------------------------
c---------------------------------------------------------------------

c---------------------------------------------------------------------
c   to perform pseudo-time stepping SSOR iterations
c   for five nonlinear pde's.
c---------------------------------------------------------------------

      implicit none
      integer niter

      include 'applu.incl'

c---------------------------------------------------------------------
c  local variables
c---------------------------------------------------------------------
      integer i, j, k, m, n
      integer istep
      integer l, lst, lend, np(isiz1+isiz2+isiz3-3)
      integer indxp(isiz1*isiz2*3/4,isiz1+isiz2+isiz3-3),
     >        jndxp(isiz1*isiz2*3/4,isiz1+isiz2+isiz3-3)
      double precision  tmp
      double precision  delunm(5)
      common /ssor_work/ np, indxp, jndxp

      external timer_read
      double precision timer_read


 
c---------------------------------------------------------------------
c   begin pseudo-time stepping iterations
c---------------------------------------------------------------------
      tmp = 1.0d+00 / ( omega * ( 2.0d+00 - omega ) ) 
      lst = ist + jst + 2
      lend = iend + jend + nz - 1

c---------------------------------------------------------------------
c   initialize a,b,c,d to zero (guarantees that page tables have been
c   formed, if applicable on given architecture, before timestepping).
c---------------------------------------------------------------------
      do l=1,isiz1*isiz2
         do n=1,5
            do m=1,5
               a(m,n,l) = 0.d0
               b(m,n,l) = 0.d0
               c(m,n,l) = 0.d0
               d(m,n,l) = 0.d0
            enddo
         enddo
      enddo
      do i = 1, t_last
         call timer_clear(i)
      end do

c---------------------------------------------------------------------
c   compute the steady-state residuals
c---------------------------------------------------------------------
      call rhs
 
c---------------------------------------------------------------------
c   compute the L2 norms of newton iteration residuals
c---------------------------------------------------------------------
      call l2norm( isiz1, isiz2, isiz3, nx0, ny0, nz0,
     >             ist, iend, jst, jend,
     >             rsd, rsdnm )


c      if ( ipr .eq. 1 ) then
c         write (*,*) '          Initial residual norms'
c         write (*,*)
c         write (*,1007) ( rsdnm(m), m = 1, 5 )
c         write (*,'(/a)') 'Iteration RMS-residual of 5th PDE'
c      end if
 
 
      do i = 1, t_last
         call timer_clear(i)
      end do
      call timer_start(1)

      call calcnp(lst, lend, np, indxp, jndxp)
 
c---------------------------------------------------------------------
c   the timestep loop
c---------------------------------------------------------------------
      do istep = 1, niter

         
c         if ( ( mod ( istep, inorm ) .eq. 0 ) .and.
c     >          ipr .eq. 1 ) then
c             write ( *, 1001 ) istep
c         end if
         if (mod ( istep, 20) .eq. 0 .or.
     >         istep .eq. itmax .or.
     >         istep .eq. 1) then
            if (niter .gt. 1) write( *, 200) istep
 200        format(' Time step ', i4)
         endif
 
c---------------------------------------------------------------------
c   perform SSOR iteration
c---------------------------------------------------------------------
         if (timeron) call timer_start(t_rhs)
         do k = 2, nz - 1
            do j = jst, jend
               do i = ist, iend
                  do m = 1, 5
                     rsd(m,i,j,k) = dt * rsd(m,i,j,k)
                  end do
               end do
            end do
         end do
         if (timeron) call timer_stop(t_rhs)
 
         do l = lst, lend
c---------------------------------------------------------------------
c   form the lower triangular part of the jacobian matrix
c---------------------------------------------------------------------
            if (timeron) call timer_start(t_jacld)
            call jacld(l, np(l), indxp(1,l), jndxp(1,l))
            if (timeron) call timer_stop(t_jacld)
 
c---------------------------------------------------------------------
c   perform the lower triangular solution
c---------------------------------------------------------------------
            if (timeron) call timer_start(t_blts)
            call blts( isiz1, isiz2, isiz3,
     >                 nx, ny, nz, l,
     >                 omega,
     >                 rsd, 
     >                 a, b, c, d,
     >                 np(l), indxp(1,l), jndxp(1,l) )
            if (timeron) call timer_stop(t_blts)
         end do
 
         do l = lend, lst, -1
c---------------------------------------------------------------------
c   form the strictly upper triangular part of the jacobian matrix
c---------------------------------------------------------------------
            if (timeron) call timer_start(t_jacu)
            call jacu(l, np(l), indxp(1,l), jndxp(1,l))
            if (timeron) call timer_stop(t_jacu)

c---------------------------------------------------------------------
c   perform the upper triangular solution
c---------------------------------------------------------------------
            if (timeron) call timer_start(t_buts)
            call buts( isiz1, isiz2, isiz3,
     >                 nx, ny, nz, l,
     >                 omega,
     >                 rsd, 
     >                 d, a, b, c,
     >                 np(l), indxp(1,l), jndxp(1,l) )
            if (timeron) call timer_stop(t_buts)
         end do
 
c---------------------------------------------------------------------
c   update the variables
c---------------------------------------------------------------------

         if (timeron) call timer_start(t_add)
         do k = 2, nz-1
            do j = jst, jend
               do i = ist, iend
                  do m = 1, 5
                     u( m, i, j, k ) = u( m, i, j, k )
     >                    + tmp * rsd( m, i, j, k )
                  end do
               end do
            end do
         end do
         if (timeron) call timer_stop(t_add)
 
c---------------------------------------------------------------------
c   compute the max-norms of newton iteration corrections
c---------------------------------------------------------------------
         if ( mod ( istep, inorm ) .eq. 0 ) then
            if (timeron) call timer_start(t_l2norm)
            call l2norm( isiz1, isiz2, isiz3, nx0, ny0, nz0,
     >                   ist, iend, jst, jend,
     >                   rsd, delunm )
            if (timeron) call timer_stop(t_l2norm)
c            if ( ipr .eq. 1 ) then
c                write (*,1006) ( delunm(m), m = 1, 5 )
c            else if ( ipr .eq. 2 ) then
c                write (*,'(i5,f15.6)') istep,delunm(5)
c            end if
         end if
 
c---------------------------------------------------------------------
c   compute the steady-state residuals
c---------------------------------------------------------------------
         if (timeron) call timer_start(t_rhs)
         call rhs
         if (timeron) call timer_stop(t_rhs)
 
c---------------------------------------------------------------------
c   compute the max-norms of newton iteration residuals
c---------------------------------------------------------------------
         if ( ( mod ( istep, inorm ) .eq. 0 ) .or.
     >        ( istep .eq. itmax ) ) then
            if (timeron) call timer_start(t_l2norm)
            call l2norm( isiz1, isiz2, isiz3, nx0, ny0, nz0,
     >                   ist, iend, jst, jend,
     >                   rsd, rsdnm )
            if (timeron) call timer_stop(t_l2norm)
c            if ( ipr .eq. 1 ) then
c                write (*,1007) ( rsdnm(m), m = 1, 5 )
c            end if
         end if

c---------------------------------------------------------------------
c   check the newton-iteration residuals against the tolerance levels
c---------------------------------------------------------------------
         if ( ( rsdnm(1) .lt. tolrsd(1) ) .and.
     >        ( rsdnm(2) .lt. tolrsd(2) ) .and.
     >        ( rsdnm(3) .lt. tolrsd(3) ) .and.
     >        ( rsdnm(4) .lt. tolrsd(4) ) .and.
     >        ( rsdnm(5) .lt. tolrsd(5) ) ) then
c            if (ipr .eq. 1 ) then
c               write (*,1004) istep
c            end if
            return
         end if
 
      end do
 
      call timer_stop(1)
      maxtime= timer_read(1)
 


      return
      
 1001 format (1x/5x,'pseudo-time SSOR iteration no.=',i4/)
 1004 format (1x/1x,'convergence was achieved after ',i4,
     >   ' pseudo-time steps' )
 1006 format (1x/1x,'RMS-norm of SSOR-iteration correction ',
     > 'for first pde  = ',1pe12.5/,
     > 1x,'RMS-norm of SSOR-iteration correction ',
     > 'for second pde = ',1pe12.5/,
     > 1x,'RMS-norm of SSOR-iteration correction ',
     > 'for third pde  = ',1pe12.5/,
     > 1x,'RMS-norm of SSOR-iteration correction ',
     > 'for fourth pde = ',1pe12.5/,
     > 1x,'RMS-norm of SSOR-iteration correction ',
     > 'for fifth pde  = ',1pe12.5)
 1007 format (1x/1x,'RMS-norm of steady-state residual for ',
     > 'first pde  = ',1pe12.5/,
     > 1x,'RMS-norm of steady-state residual for ',
     > 'second pde = ',1pe12.5/,
     > 1x,'RMS-norm of steady-state residual for ',
     > 'third pde  = ',1pe12.5/,
     > 1x,'RMS-norm of steady-state residual for ',
     > 'fourth pde = ',1pe12.5/,
     > 1x,'RMS-norm of steady-state residual for ',
     > 'fifth pde  = ',1pe12.5)
 
      end
