
c---------------------------------------------------------------------
c---------------------------------------------------------------------

       subroutine  initialize

c---------------------------------------------------------------------
c---------------------------------------------------------------------

c---------------------------------------------------------------------
c This subroutine initializes the field variable u using 
c tri-linear transfinite interpolation of the boundary values     
c---------------------------------------------------------------------

       include 'header.h'
  
       integer i, j, k, m, ix, iy, iz
       double precision  xi, eta, zeta, Pface(5,3,2), Pxi, Peta, 
     >                   Pzeta, temp(5)
    
c---------------------------------------------------------------------
c  Later (in compute_rhs) we compute 1/u for every element. A few of 
c  the corner elements are not used, but it convenient (and faster) 
c  to compute the whole thing with a simple loop. Make sure those 
c  values are nonzero by initializing the whole thing here. 
c---------------------------------------------------------------------
!$omp parallel default(shared) private(i,j,k,m)
!$omp do
      do k = 0, grid_points(3)-1
         do j = 0, grid_points(2)-1
            do i = 0, grid_points(1)-1
               u(1,i,j,k) = 1.0
               u(2,i,j,k) = 0.0
               u(3,i,j,k) = 0.0
               u(4,i,j,k) = 0.0
               u(5,i,j,k) = 1.0
               us(i,j,k) = 0.0
               vs(i,j,k) = 0.0
               ws(i,j,k) = 0.0
               qs(i,j,k) = 0.0
               rho_i(i,j,k) = 0.0
               speed(i,j,k) = 0.0
               square(i,j,k) = 0.0
               qs(i,j,k) = 0.0
            end do
         end do
      end do
!$omp end do nowait
!$omp do
      do k = 0, grid_points(3)-1
         do j = 0, grid_points(2)-1
            do i = 0, grid_points(1)-1
               do m = 1, 5
                  rhs(m,i,j,k) = 0.0
               end do
            end do
         end do
      end do
!$omp end do nowait
!$omp end parallel

c---------------------------------------------------------------------
c first store the "interpolated" values everywhere on the grid    
c---------------------------------------------------------------------
!$omp parallel default(shared)
!$omp& private(i,j,k,m,zeta,eta,xi,ix,iy,iz,Pxi,Peta,Pzeta,Pface,temp)
!$omp do schedule(static)
          do  k = 0, grid_points(3)-1
             zeta = dble(k) * dnzm1
             do  j = 0, grid_points(2)-1
                eta = dble(j) * dnym1
                do   i = 0, grid_points(1)-1
                   xi = dble(i) * dnxm1
                  
                   do ix = 1, 2
                      Pxi = dble(ix-1)
                      call exact_solution(Pxi, eta, zeta, 
     >                                    Pface(1,1,ix))
                   end do

                   do    iy = 1, 2
                      Peta = dble(iy-1)
                      call exact_solution(xi, Peta, zeta, 
     >                                    Pface(1,2,iy))
                   end do

                   do    iz = 1, 2
                      Pzeta = dble(iz-1)
                      call exact_solution(xi, eta, Pzeta,   
     >                                    Pface(1,3,iz))
                   end do

                   do   m = 1, 5
                      Pxi   = xi   * Pface(m,1,2) + 
     >                        (1.0d0-xi)   * Pface(m,1,1)
                      Peta  = eta  * Pface(m,2,2) + 
     >                        (1.0d0-eta)  * Pface(m,2,1)
                      Pzeta = zeta * Pface(m,3,2) + 
     >                        (1.0d0-zeta) * Pface(m,3,1)
 
                      u(m,i,j,k) = Pxi + Peta + Pzeta - 
     >                          Pxi*Peta - Pxi*Pzeta - Peta*Pzeta + 
     >                          Pxi*Peta*Pzeta

                   end do
                end do
             end do
          end do
!$omp end do nowait

c---------------------------------------------------------------------
c now store the exact values on the boundaries        
c---------------------------------------------------------------------

c---------------------------------------------------------------------
c west face                                                  
c---------------------------------------------------------------------

       xi = 0.0d0
       i  = 0
!$omp do schedule(static)
       do  k = 0, grid_points(3)-1
          zeta = dble(k) * dnzm1
          do   j = 0, grid_points(2)-1
             eta = dble(j) * dnym1
             call exact_solution(xi, eta, zeta, temp)
             do   m = 1, 5
                u(m,i,j,k) = temp(m)
             end do
          end do
       end do
!$omp end do nowait

c---------------------------------------------------------------------
c east face                                                      
c---------------------------------------------------------------------

       xi = 1.0d0
       i  = grid_points(1)-1
!$omp do schedule(static)
       do   k = 0, grid_points(3)-1
          zeta = dble(k) * dnzm1
          do   j = 0, grid_points(2)-1
             eta = dble(j) * dnym1
             call exact_solution(xi, eta, zeta, temp)
             do   m = 1, 5
                u(m,i,j,k) = temp(m)
             end do
          end do
       end do
!$omp end do nowait

c---------------------------------------------------------------------
c south face                                                 
c---------------------------------------------------------------------

       eta = 0.0d0
       j   = 0
!$omp do schedule(static)
       do  k = 0, grid_points(3)-1
          zeta = dble(k) * dnzm1
          do   i = 0, grid_points(1)-1
             xi = dble(i) * dnxm1
             call exact_solution(xi, eta, zeta, temp)
             do   m = 1, 5
                u(m,i,j,k) = temp(m)
             end do
          end do
       end do
!$omp end do nowait


c---------------------------------------------------------------------
c north face                                    
c---------------------------------------------------------------------

       eta = 1.0d0
       j   = grid_points(2)-1
!$omp do schedule(static)
       do   k = 0, grid_points(3)-1
          zeta = dble(k) * dnzm1
          do   i = 0, grid_points(1)-1
             xi = dble(i) * dnxm1
             call exact_solution(xi, eta, zeta, temp)
             do   m = 1, 5
                u(m,i,j,k) = temp(m)
             end do
          end do
       end do
!$omp end do

c---------------------------------------------------------------------
c bottom face                                       
c---------------------------------------------------------------------

       zeta = 0.0d0
       k    = 0
!$omp do schedule(static)
       do   j = 0, grid_points(2)-1
          eta = dble(j) * dnym1
          do   i =0, grid_points(1)-1
             xi = dble(i) *dnxm1
             call exact_solution(xi, eta, zeta, temp)
             do   m = 1, 5
                u(m,i,j,k) = temp(m)
             end do
          end do
       end do
!$omp end do nowait

c---------------------------------------------------------------------
c top face     
c---------------------------------------------------------------------

       zeta = 1.0d0
       k    = grid_points(3)-1
!$omp do schedule(static)
       do   j = 0, grid_points(2)-1
          eta = dble(j) * dnym1
          do   i =0, grid_points(1)-1
             xi = dble(i) * dnxm1
             call exact_solution(xi, eta, zeta, temp)
             do   m = 1, 5
                u(m,i,j,k) = temp(m)
             end do
          end do
       end do
!$omp end do nowait
!$omp end parallel

       return
       end


       subroutine lhsinit(lhs,lhsp,lhsm,ni)
       integer i, n, ni
       double precision lhs(5,0:ni),lhsp(5,0:ni),lhsm(5,0:ni)

c---------------------------------------------------------------------
c     zap the whole left hand side for starters
c     set all diagonal values to 1. This is overkill, but convenient
c---------------------------------------------------------------------
       i = 0
       do   n = 1, 5
          lhs(n,i) = 0.0d0
          lhsp(n,i) = 0.0d0
          lhsm(n,i) = 0.0d0
       end do
       lhs(3,i) = 1.0d0
       lhsp(3,i) = 1.0d0
       lhsm(3,i) = 1.0d0

       i = ni
       do   n = 1, 5
          lhs(n,i) = 0.0d0
          lhsp(n,i) = 0.0d0
          lhsm(n,i) = 0.0d0
       end do
       lhs(3,i) = 1.0d0
       lhsp(3,i) = 1.0d0
       lhsm(3,i) = 1.0d0
 
       return
       end



