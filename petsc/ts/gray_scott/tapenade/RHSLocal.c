#include "ex5.h"

void RHSLocal(Field **f,Field **u,int gxs,int gxm,int gys,int gym,double hx,double hy,void *ptr)
{
  AppCtx   *appctx = (AppCtx*)ptr;
  int      i,j,sx,sy;
  double   uc,uxx,uyy,vc,vxx,vyy;

  sx = 1.0/(hx*hx);
  sy = 1.0/(hy*hy);

  for (j=gys; j<gys+gym; j++) {
    for (i=gxs; i<gxs+gxm; i++) {
      uc        = u[j][i].u;
      uxx       = (-2.0*uc + u[j][i-1].u + u[j][i+1].u)*sx;
      uyy       = (-2.0*uc + u[j-1][i].u + u[j+1][i].u)*sy;
      vc        = u[j][i].v;
      vxx       = (-2.0*vc + u[j][i-1].v + u[j][i+1].v)*sx;
      vyy       = (-2.0*vc + u[j-1][i].v + u[j+1][i].v)*sy;
      f[j][i].u = appctx->D1*(uxx + uyy) - uc*vc*vc + appctx->gamma*(1.0 - uc);
      f[j][i].v = appctx->D2*(vxx + vyy) + uc*vc*vc - (appctx->gamma + appctx->kappa)*vc;
    }
  }
}
