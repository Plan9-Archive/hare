
c---------------------------------------------------------------------
c---------------------------------------------------------------------

        subroutine verify(no_time_steps, class, verified)

c---------------------------------------------------------------------
c---------------------------------------------------------------------

c---------------------------------------------------------------------
c  verification routine                         
c---------------------------------------------------------------------

        include 'header.h'

        double precision xcrref(5),xceref(5),xcrdif(5),xcedif(5), 
     >                   epsilon, xce(5), xcr(5), dtref
        integer m, no_time_steps
        character class
        logical verified

c---------------------------------------------------------------------
c   tolerance level
c---------------------------------------------------------------------
        epsilon = 1.0d-08


c---------------------------------------------------------------------
c   compute the error norm and the residual norm, and exit if not printing
c---------------------------------------------------------------------
        call error_norm(xce)
        call compute_rhs

        call rhs_norm(xcr)

        do m = 1, 5
           xcr(m) = xcr(m) / dt
        enddo


        class = 'U'
        verified = .true.

        do m = 1,5
           xcrref(m) = 1.0
           xceref(m) = 1.0
        end do

c---------------------------------------------------------------------
c    reference data for 12X12X12 grids after 60 time steps, with DT = 1.0d-02
c---------------------------------------------------------------------
        if ( (grid_points(1)  .eq. 12     ) .and. 
     >       (grid_points(2)  .eq. 12     ) .and.
     >       (grid_points(3)  .eq. 12     ) .and.
     >       (no_time_steps   .eq. 60    ))  then

           class = 'S'
           dtref = 1.0d-2

c---------------------------------------------------------------------
c  Reference values of RMS-norms of residual.
c---------------------------------------------------------------------
         xcrref(1) = 1.7034283709541311d-01
         xcrref(2) = 1.2975252070034097d-02
         xcrref(3) = 3.2527926989486055d-02
         xcrref(4) = 2.6436421275166801d-02
         xcrref(5) = 1.9211784131744430d-01

c---------------------------------------------------------------------
c  Reference values of RMS-norms of solution error.
c---------------------------------------------------------------------
         xceref(1) = 4.9976913345811579d-04
         xceref(2) = 4.5195666782961927d-05
         xceref(3) = 7.3973765172921357d-05
         xceref(4) = 7.3821238632439731d-05
         xceref(5) = 8.9269630987491446d-04

c---------------------------------------------------------------------
c    reference data for 24X24X24 grids after 200 time steps, with DT = 0.8d-3
c---------------------------------------------------------------------
        elseif ( (grid_points(1) .eq. 24) .and. 
     >           (grid_points(2) .eq. 24) .and.
     >           (grid_points(3) .eq. 24) .and.
     >           (no_time_steps . eq. 200) ) then

           class = 'W'
           dtref = 0.8d-3
c---------------------------------------------------------------------
c  Reference values of RMS-norms of residual.
c---------------------------------------------------------------------
           xcrref(1) = 0.1125590409344d+03
           xcrref(2) = 0.1180007595731d+02
           xcrref(3) = 0.2710329767846d+02
           xcrref(4) = 0.2469174937669d+02
           xcrref(5) = 0.2638427874317d+03

c---------------------------------------------------------------------
c  Reference values of RMS-norms of solution error.
c---------------------------------------------------------------------
           xceref(1) = 0.4419655736008d+01
           xceref(2) = 0.4638531260002d+00
           xceref(3) = 0.1011551749967d+01
           xceref(4) = 0.9235878729944d+00
           xceref(5) = 0.1018045837718d+02


c---------------------------------------------------------------------
c    reference data for 64X64X64 grids after 200 time steps, with DT = 0.8d-3
c---------------------------------------------------------------------
        elseif ( (grid_points(1) .eq. 64) .and. 
     >           (grid_points(2) .eq. 64) .and.
     >           (grid_points(3) .eq. 64) .and.
     >           (no_time_steps . eq. 200) ) then

           class = 'A'
           dtref = 0.8d-3
c---------------------------------------------------------------------
c  Reference values of RMS-norms of residual.
c---------------------------------------------------------------------
         xcrref(1) = 1.0806346714637264d+02
         xcrref(2) = 1.1319730901220813d+01
         xcrref(3) = 2.5974354511582465d+01
         xcrref(4) = 2.3665622544678910d+01
         xcrref(5) = 2.5278963211748344d+02

c---------------------------------------------------------------------
c  Reference values of RMS-norms of solution error.
c---------------------------------------------------------------------
         xceref(1) = 4.2348416040525025d+00
         xceref(2) = 4.4390282496995698d-01
         xceref(3) = 9.6692480136345650d-01
         xceref(4) = 8.8302063039765474d-01
         xceref(5) = 9.7379901770829278d+00

c---------------------------------------------------------------------
c    reference data for 102X102X102 grids after 200 time steps,
c    with DT = 3.0d-04
c---------------------------------------------------------------------
        elseif ( (grid_points(1) .eq. 102) .and. 
     >           (grid_points(2) .eq. 102) .and.
     >           (grid_points(3) .eq. 102) .and.
     >           (no_time_steps . eq. 200) ) then

           class = 'B'
           dtref = 3.0d-4

c---------------------------------------------------------------------
c  Reference values of RMS-norms of residual.
c---------------------------------------------------------------------
         xcrref(1) = 1.4233597229287254d+03
         xcrref(2) = 9.9330522590150238d+01
         xcrref(3) = 3.5646025644535285d+02
         xcrref(4) = 3.2485447959084092d+02
         xcrref(5) = 3.2707541254659363d+03

c---------------------------------------------------------------------
c  Reference values of RMS-norms of solution error.
c---------------------------------------------------------------------
         xceref(1) = 5.2969847140936856d+01
         xceref(2) = 4.4632896115670668d+00
         xceref(3) = 1.3122573342210174d+01
         xceref(4) = 1.2006925323559144d+01
         xceref(5) = 1.2459576151035986d+02

c---------------------------------------------------------------------
c    reference data for 162X162X162 grids after 200 time steps,
c    with DT = 1.0d-04
c---------------------------------------------------------------------
        elseif ( (grid_points(1) .eq. 162) .and. 
     >           (grid_points(2) .eq. 162) .and.
     >           (grid_points(3) .eq. 162) .and.
     >           (no_time_steps . eq. 200) ) then

           class = 'C'
           dtref = 1.0d-4

c---------------------------------------------------------------------
c  Reference values of RMS-norms of residual.
c---------------------------------------------------------------------
         xcrref(1) = 0.62398116551764615d+04
         xcrref(2) = 0.50793239190423964d+03
         xcrref(3) = 0.15423530093013596d+04
         xcrref(4) = 0.13302387929291190d+04
         xcrref(5) = 0.11604087428436455d+05

c---------------------------------------------------------------------
c  Reference values of RMS-norms of solution error.
c---------------------------------------------------------------------
         xceref(1) = 0.16462008369091265d+03
         xceref(2) = 0.11497107903824313d+02
         xceref(3) = 0.41207446207461508d+02
         xceref(4) = 0.37087651059694167d+02
         xceref(5) = 0.36211053051841265d+03

c---------------------------------------------------------------------
c    reference data for 408x408x408 grids after 250 time steps,
c    with DT = 0.5d-04
c---------------------------------------------------------------------
        elseif ( (grid_points(1) .eq. 408) .and. 
     >           (grid_points(2) .eq. 408) .and.
     >           (grid_points(3) .eq. 408) .and.
     >           (no_time_steps . eq. 250) ) then

           class = 'D'
           dtref = 0.2d-4

c---------------------------------------------------------------------
c  Reference values of RMS-norms of residual.
c---------------------------------------------------------------------
         xcrref(1) = 0.2533188551738d+05
         xcrref(2) = 0.2346393716980d+04
         xcrref(3) = 0.6294554366904d+04
         xcrref(4) = 0.5352565376030d+04
         xcrref(5) = 0.3905864038618d+05

c---------------------------------------------------------------------
c  Reference values of RMS-norms of solution error.
c---------------------------------------------------------------------

         xceref(1) = 0.3100009377557d+03
         xceref(2) = 0.2424086324913d+02
         xceref(3) = 0.7782212022645d+02
         xceref(4) = 0.6835623860116d+02
         xceref(5) = 0.6065737200368d+03


        else
           verified = .false.
        endif

c---------------------------------------------------------------------
c    verification test for residuals if gridsize is either 12X12X12 or 
c    64X64X64 or 102X102X102 or 162X162X162 or 408X408X408
c---------------------------------------------------------------------

c---------------------------------------------------------------------
c    Compute the difference of solution values and the known reference values.
c---------------------------------------------------------------------
        do m = 1, 5
           
           xcrdif(m) = dabs((xcr(m)-xcrref(m))/xcrref(m)) 
           xcedif(m) = dabs((xce(m)-xceref(m))/xceref(m))
           
        enddo

c---------------------------------------------------------------------
c    Output the comparison of computed results to known cases.
c---------------------------------------------------------------------

        if (class .ne. 'U') then
           write(*, 1990) class
 1990      format(' Verification being performed for class ', a)
           write (*,2000) epsilon
 2000      format(' accuracy setting for epsilon = ', E20.13)
           verified = (dabs(dt-dtref) .le. epsilon)
           if (.not.verified) then  
              class = 'U'
              write (*,1000) dtref
 1000         format(' DT does not match the reference value of ', 
     >                 E15.8)
           endif
        else 
           write(*, 1995)
 1995      format(' Unknown class')
        endif


        if (class .ne. 'U') then
           write (*, 2001) 
        else
           write (*, 2005)
        endif

 2001   format(' Comparison of RMS-norms of residual')
 2005   format(' RMS-norms of residual')
        do m = 1, 5
           if (class .eq. 'U') then
              write(*, 2015) m, xcr(m)
           else if (xcrdif(m) .le. epsilon) then
              write (*,2011) m,xcr(m),xcrref(m),xcrdif(m)
           else 
              verified = .false.
              write (*,2010) m,xcr(m),xcrref(m),xcrdif(m)
           endif
        enddo

        if (class .ne. 'U') then
           write (*,2002)
        else
           write (*,2006)
        endif
 2002   format(' Comparison of RMS-norms of solution error')
 2006   format(' RMS-norms of solution error')
        
        do m = 1, 5
           if (class .eq. 'U') then
              write(*, 2015) m, xce(m)
           else if (xcedif(m) .le. epsilon) then
              write (*,2011) m,xce(m),xceref(m),xcedif(m)
           else
              verified = .false.
              write (*,2010) m,xce(m),xceref(m),xcedif(m)
           endif
        enddo
        
 2010   format(' FAILURE: ', i2, E20.13, E20.13, E20.13)
 2011   format('          ', i2, E20.13, E20.13, E20.13)
 2015   format('          ', i2, E20.13)
        
        if (class .eq. 'U') then
           write(*, 2022)
           write(*, 2023)
 2022      format(' No reference values provided')
 2023      format(' No verification performed')
        else if (verified) then
           write(*, 2020)
 2020      format(' Verification Successful')
        else
           write(*, 2021)
 2021      format(' Verification failed')
        endif

        return


        end
