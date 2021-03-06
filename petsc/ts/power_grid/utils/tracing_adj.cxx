#include <adolc/adolc.h>
#include "init.cxx"
#include "conversion.cxx"


/* Computes F = [f(x,y);g(x,y)] */
PetscErrorCode ResidualFunctionPassive(SNES snes,Vec X, Vec F, Userctx *user)
{
  PetscErrorCode ierr;
  Vec            Xgen,Xnet,Fgen,Fnet;
  PetscScalar    *xgen,*xnet,*fgen,*fnet;
  PetscInt       i,idx=0;
  PetscScalar    Vr,Vi,Vm,Vm2;
  PetscScalar    Eqp,Edp,delta,w; /* Generator variables */
  PetscScalar    Efd,RF,VR; /* Exciter variables */
  PetscScalar    Id,Iq;  /* Generator dq axis currents */
  PetscScalar    Vd,Vq,SE;
  PetscScalar    IGr,IGi,IDr,IDi;
  PetscScalar    Zdq_inv[4],det;
  PetscScalar    PD,QD,Vm0,*v0;
  PetscInt       k;

  PetscFunctionBegin;
  ierr = VecZeroEntries(F);CHKERRQ(ierr);
  ierr = DMCompositeGetLocalVectors(user->dmpgrid,&Xgen,&Xnet);CHKERRQ(ierr);
  ierr = DMCompositeGetLocalVectors(user->dmpgrid,&Fgen,&Fnet);CHKERRQ(ierr);
  ierr = DMCompositeScatter(user->dmpgrid,X,Xgen,Xnet);CHKERRQ(ierr);
  ierr = DMCompositeScatter(user->dmpgrid,F,Fgen,Fnet);CHKERRQ(ierr);

  /* Network current balance residual IG + Y*V + IL = 0. Only YV is added here.
     The generator current injection, IG, and load current injection, ID are added later
  */
  /* Note that the values in Ybus are stored assuming the imaginary current balance
     equation is ordered first followed by real current balance equation for each bus.
     Thus imaginary current contribution goes in location 2*i, and
     real current contribution in 2*i+1
  */
  ierr = MatMult(user->Ybus,Xnet,Fnet);CHKERRQ(ierr);

  ierr = VecGetArray(Xgen,&xgen);CHKERRQ(ierr);
  ierr = VecGetArray(Xnet,&xnet);CHKERRQ(ierr);
  ierr = VecGetArray(Fgen,&fgen);CHKERRQ(ierr);
  ierr = VecGetArray(Fnet,&fnet);CHKERRQ(ierr);

  /* Generator subsystem */
  for (i=0; i < ngen; i++) {
    Eqp   = xgen[idx];
    Edp   = xgen[idx+1];
    delta = xgen[idx+2];
    w     = xgen[idx+3];
    Id    = xgen[idx+4];
    Iq    = xgen[idx+5];
    Efd   = xgen[idx+6];
    RF    = xgen[idx+7];
    VR    = xgen[idx+8];
    /* Generator differential equations */
    fgen[idx]   = (Eqp + (Xd[i] - Xdp[i])*Id - Efd)/Td0p[i];
    fgen[idx+1] = (Edp - (Xq[i] - Xqp[i])*Iq)/Tq0p[i];
    fgen[idx+2] = -w + w_s;
    fgen[idx+3] = (-TM[i] + Edp*Id + Eqp*Iq + (Xqp[i] - Xdp[i])*Id*Iq + D[i]*(w - w_s))/M[i];

    Vr = xnet[2*gbus[i]]; /* Real part of generator terminal voltage */
    Vi = xnet[2*gbus[i]+1]; /* Imaginary part of the generator terminal voltage */

    ierr = ri2dq(Vr,Vi,delta,&Vd,&Vq);CHKERRQ(ierr);

    /* Algebraic equations for stator currents */
    det = Rs[i]*Rs[i] + Xdp[i]*Xqp[i];

    Zdq_inv[0] = Rs[i]/det;
    Zdq_inv[1] = Xqp[i]/det;
    Zdq_inv[2] = -Xdp[i]/det;
    Zdq_inv[3] = Rs[i]/det;

    fgen[idx+4] = Zdq_inv[0]*(-Edp + Vd) + Zdq_inv[1]*(-Eqp + Vq) + Id;
    fgen[idx+5] = Zdq_inv[2]*(-Edp + Vd) + Zdq_inv[3]*(-Eqp + Vq) + Iq;

    /* Add generator current injection to network */
    ierr = dq2ri(Id,Iq,delta,&IGr,&IGi);CHKERRQ(ierr);

    fnet[2*gbus[i]]   -= IGi;
    fnet[2*gbus[i]+1] -= IGr;

    Vm = PetscSqrtScalar(Vd*Vd + Vq*Vq);

    SE = k1[i]*PetscExpScalar(k2[i]*Efd);

    /* Exciter differential equations */
    fgen[idx+6] = (KE[i]*Efd + SE - VR)/TE[i];
    fgen[idx+7] = (RF - KF[i]*Efd/TF[i])/TF[i];
    fgen[idx+8] = (VR - KA[i]*RF + KA[i]*KF[i]*Efd/TF[i] - KA[i]*(Vref[i] - Vm))/TA[i];

    idx = idx + 9;
  }

  ierr = VecGetArray(user->V0,&v0);CHKERRQ(ierr);
  for (i=0; i < nload; i++) {
    Vr  = xnet[2*lbus[i]]; /* Real part of load bus voltage */
    Vi  = xnet[2*lbus[i]+1]; /* Imaginary part of the load bus voltage */
    Vm  = PetscSqrtScalar(Vr*Vr + Vi*Vi); Vm2 = Vm*Vm;
    Vm0 = PetscSqrtScalar(v0[2*lbus[i]]*v0[2*lbus[i]] + v0[2*lbus[i]+1]*v0[2*lbus[i]+1]);
    PD  = QD = 0.0;
    for (k=0; k < ld_nsegsp[i]; k++) PD += ld_alphap[k]*PD0[i]*PetscPowScalar((Vm/Vm0),ld_betap[k]);
    for (k=0; k < ld_nsegsq[i]; k++) QD += ld_alphaq[k]*QD0[i]*PetscPowScalar((Vm/Vm0),ld_betaq[k]);

    /* Load currents */
    IDr = (PD*Vr + QD*Vi)/Vm2;
    IDi = (-QD*Vr + PD*Vi)/Vm2;

    fnet[2*lbus[i]]   += IDi;
    fnet[2*lbus[i]+1] += IDr;
  }
  ierr = VecRestoreArray(user->V0,&v0);CHKERRQ(ierr);

  ierr = VecRestoreArray(Xgen,&xgen);CHKERRQ(ierr);
  ierr = VecRestoreArray(Xnet,&xnet);CHKERRQ(ierr);
  ierr = VecRestoreArray(Fgen,&fgen);CHKERRQ(ierr);
  ierr = VecRestoreArray(Fnet,&fnet);CHKERRQ(ierr);

  ierr = DMCompositeGather(user->dmpgrid,INSERT_VALUES,F,Fgen,Fnet);CHKERRQ(ierr);
  ierr = DMCompositeRestoreLocalVectors(user->dmpgrid,&Xgen,&Xnet);CHKERRQ(ierr);
  ierr = DMCompositeRestoreLocalVectors(user->dmpgrid,&Fgen,&Fnet);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode ResidualFunctionActive(SNES snes,Vec X, Vec F, Userctx *user)
{
  PetscErrorCode ierr;
  Vec            Xgen,Xnet,Fgen,Fnet;
  PetscScalar    *xgen_p,*xnet_p,*fgen_p,*fnet_p;
  PetscInt       i,idx=0;
  adouble        Vr,Vi,Vm,Vm2;
  adouble        Eqp,Edp,delta,w; /* Generator variables */
  adouble        Efd,RF,VR; /* Exciter variables */
  adouble        Id,Iq;  /* Generator dq axis currents */
  adouble        Vd,Vq,SE;
  adouble        IGr,IGi,IDr,IDi;
  adouble        Zdq_inv[4],det;
  adouble        PD,QD,Vm0;
  PetscScalar    *v0;
  PetscInt       k;
  adouble        *xgen = user->xgen_a,*xnet = user->xnet_a,*fgen = user->fgen_a,*fnet = user->fnet_a;

  PetscFunctionBegin;
  ierr = VecZeroEntries(F);CHKERRQ(ierr);
  ierr = DMCompositeGetLocalVectors(user->dmpgrid,&Xgen,&Xnet);CHKERRQ(ierr);
  ierr = DMCompositeGetLocalVectors(user->dmpgrid,&Fgen,&Fnet);CHKERRQ(ierr);
  ierr = DMCompositeScatter(user->dmpgrid,X,Xgen,Xnet);CHKERRQ(ierr);
  ierr = DMCompositeScatter(user->dmpgrid,F,Fgen,Fnet);CHKERRQ(ierr);

  /* Network current balance residual IG + Y*V + IL = 0. Only YV is added here.
     The generator current injection, IG, and load current injection, ID are added later
  */
  /* Note that the values in Ybus are stored assuming the imaginary current balance
     equation is ordered first followed by real current balance equation for each bus.
     Thus imaginary current contribution goes in location 2*i, and
     real current contribution in 2*i+1
  */
  ierr = MatMult(user->Ybus,Xnet,Fnet);CHKERRQ(ierr);

  ierr = VecGetArray(Xgen,&xgen_p);CHKERRQ(ierr);
  ierr = VecGetArray(Xnet,&xnet_p);CHKERRQ(ierr);
  ierr = VecGetArray(Fgen,&fgen_p);CHKERRQ(ierr);
  ierr = VecGetArray(Fnet,&fnet_p);CHKERRQ(ierr);

  trace_on(1);

  /* Mark independent variables */
  for (i=0; i<user->neqs_gen; i++)
    xgen[i] <<= xgen_p[i];
  for (i=0; i<user->neqs_net; i++)
    xnet[i] <<= xnet_p[i];

  /* Generator subsystem */
  for (i=0; i < ngen; i++) {
    Eqp   = xgen[idx];
    Edp   = xgen[idx+1];
    delta = xgen[idx+2];
    w     = xgen[idx+3];
    Id    = xgen[idx+4];
    Iq    = xgen[idx+5];
    Efd   = xgen[idx+6];
    RF    = xgen[idx+7];
    VR    = xgen[idx+8];

    /* Generator differential equations */
    fgen[idx]   = (Eqp + (Xd[i] - Xdp[i])*Id - Efd)/Td0p[i];
    fgen[idx+1] = (Edp - (Xq[i] - Xqp[i])*Iq)/Tq0p[i];
    fgen[idx+2] = -w + w_s;
    fgen[idx+3] = (-TM[i] + Edp*Id + Eqp*Iq + (Xqp[i] - Xdp[i])*Id*Iq + D[i]*(w - w_s))/M[i];

    Vr = xnet[2*gbus[i]]; /* Real part of generator terminal voltage */
    Vi = xnet[2*gbus[i]+1]; /* Imaginary part of the generator terminal voltage */

    ierr = ri2dq(Vr,Vi,delta,&Vd,&Vq);CHKERRQ(ierr);

    /* Algebraic equations for stator currents */
    det = Rs[i]*Rs[i] + Xdp[i]*Xqp[i];

    Zdq_inv[0] = Rs[i]/det;
    Zdq_inv[1] = Xqp[i]/det;
    Zdq_inv[2] = -Xdp[i]/det;
    Zdq_inv[3] = Rs[i]/det;

    fgen[idx+4] = Zdq_inv[0]*(-Edp + Vd) + Zdq_inv[1]*(-Eqp + Vq) + Id;
    fgen[idx+5] = Zdq_inv[2]*(-Edp + Vd) + Zdq_inv[3]*(-Eqp + Vq) + Iq;

    /* Add generator current injection to network */
    ierr = dq2ri(Id,Iq,delta,&IGr,&IGi);CHKERRQ(ierr);

    fnet[2*gbus[i]]   -= IGi;
    fnet[2*gbus[i]+1] -= IGr;

    Vm = PetscSqrtScalar(Vd*Vd + Vq*Vq);

    SE = k1[i]*PetscExpScalar(k2[i]*Efd);

    /* Exciter differential equations */
    fgen[idx+6] = (KE[i]*Efd + SE - VR)/TE[i];
    fgen[idx+7] = (RF - KF[i]*Efd/TF[i])/TF[i];
    fgen[idx+8] = (VR - KA[i]*RF + KA[i]*KF[i]*Efd/TF[i] - KA[i]*(Vref[i] - Vm))/TA[i];

    idx = idx + 9;
  }

  ierr = VecGetArray(user->V0,&v0);CHKERRQ(ierr);
  for (i=0; i < nload; i++) {
    Vr  = xnet[2*lbus[i]]; /* Real part of load bus voltage */
    Vi  = xnet[2*lbus[i]+1]; /* Imaginary part of the load bus voltage */
    Vm  = PetscSqrtScalar(Vr*Vr + Vi*Vi); Vm2 = Vm*Vm;
    Vm0 = PetscSqrtScalar(v0[2*lbus[i]]*v0[2*lbus[i]] + v0[2*lbus[i]+1]*v0[2*lbus[i]+1]);
    PD  = QD = 0.0;
    for (k=0; k < ld_nsegsp[i]; k++) PD += ld_alphap[k]*PD0[i]*PetscPowScalar((Vm/Vm0),ld_betap[k]);
    for (k=0; k < ld_nsegsq[i]; k++) QD += ld_alphaq[k]*QD0[i]*PetscPowScalar((Vm/Vm0),ld_betaq[k]);

    /* Load currents */
    IDr = (PD*Vr + QD*Vi)/Vm2;
    IDi = (-QD*Vr + PD*Vi)/Vm2;

    fnet[2*lbus[i]]   += IDi;
    fnet[2*lbus[i]+1] += IDr;
  }
  ierr = VecRestoreArray(user->V0,&v0);CHKERRQ(ierr);

  /* Mark dependent variables */
  for (i=0; i<user->neqs_gen; i++)
    fgen[i] >>= fgen_p[i];
  for (i=0; i<user->neqs_net; i++)
    fnet[i] >>= fnet_p[i];

  trace_off();

  ierr = VecRestoreArray(Xgen,&xgen_p);CHKERRQ(ierr);
  ierr = VecRestoreArray(Xnet,&xnet_p);CHKERRQ(ierr);
  ierr = VecRestoreArray(Fgen,&fgen_p);CHKERRQ(ierr);
  ierr = VecRestoreArray(Fnet,&fnet_p);CHKERRQ(ierr);

  ierr = DMCompositeGather(user->dmpgrid,INSERT_VALUES,F,Fgen,Fnet);CHKERRQ(ierr);
  ierr = DMCompositeRestoreLocalVectors(user->dmpgrid,&Xgen,&Xnet);CHKERRQ(ierr);
  ierr = DMCompositeRestoreLocalVectors(user->dmpgrid,&Fgen,&Fnet);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* \dot{x} - f(x,y)
     g(x,y) = 0

   TODO: trace on tape 2
 */
PetscErrorCode IFunctionPassive(TS ts,PetscReal t, Vec X, Vec Xdot, Vec F, Userctx *user)
{
  PetscErrorCode    ierr;
  SNES              snes;
  PetscScalar       *f;
  const PetscScalar *xdot;
  PetscInt          i;

  PetscFunctionBegin;
  user->t = t;

  ierr = TSGetSNES(ts,&snes);CHKERRQ(ierr);
  ierr = ResidualFunctionPassive(snes,X,F,user);CHKERRQ(ierr);
  ierr = VecGetArray(F,&f);CHKERRQ(ierr);
  ierr = VecGetArrayRead(Xdot,&xdot);CHKERRQ(ierr);
  for (i=0;i < ngen;i++) {
    f[9*i]   += xdot[9*i];
    f[9*i+1] += xdot[9*i+1];
    f[9*i+2] += xdot[9*i+2];
    f[9*i+3] += xdot[9*i+3];
    f[9*i+6] += xdot[9*i+6];
    f[9*i+7] += xdot[9*i+7];
    f[9*i+8] += xdot[9*i+8];
  }
  ierr = VecRestoreArray(F,&f);CHKERRQ(ierr);
  ierr = VecRestoreArrayRead(Xdot,&xdot);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode IFunctionActive(TS ts,PetscReal t, Vec X, Vec Xdot, Vec F, Userctx *user)
{
  PetscErrorCode    ierr;
  SNES              snes;
  PetscScalar       *f_p;
  const PetscScalar *xdot_p;
  PetscInt          i,idx = 0;
  adouble           *xdot = user->xdot_a,*f = user->fgen_a,*fnet = user->fnet_a;

  PetscFunctionBegin;
  user->t = t;

  ierr = TSGetSNES(ts,&snes);CHKERRQ(ierr);
  ierr = ResidualFunctionActive(snes,X,F,user);CHKERRQ(ierr);
  ierr = VecGetArray(F,&f_p);CHKERRQ(ierr);
  ierr = VecGetArrayRead(Xdot,&xdot_p);CHKERRQ(ierr);

  trace_on(2);

  /* Mark independence */
  for (i=0; i<user->neqs_pgrid; i++)
    xdot[i] <<= xdot_p[i];

  for (i=0;i < ngen;i++) {
    f[9*i]   += xdot[9*i];
    f[9*i+1] += xdot[9*i+1];
    f[9*i+2] += xdot[9*i+2];
    f[9*i+3] += xdot[9*i+3];
    f[9*i+6] += xdot[9*i+6];
    f[9*i+7] += xdot[9*i+7];
    f[9*i+8] += xdot[9*i+8];
  }

  /* Mark independence */
  for (i=0; i<user->neqs_gen; i++) {
    f[i] >>= f_p[idx];
    idx++;
  }
  for (i=0; i<user->neqs_net; i++) {
    fnet[i] >>= f_p[idx];
    idx++;
  }

  trace_off();

  ierr = VecRestoreArray(F,&f_p);CHKERRQ(ierr);
  ierr = VecRestoreArrayRead(Xdot,&xdot_p);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* This function is used for solving the algebraic system only during fault on and
   off times. It computes the entire F and then zeros out the part corresponding to
   differential equations
 F = [0;g(y)];

   TODO: trace?
*/
PetscErrorCode AlgFunction(SNES snes, Vec X, Vec F, void *ctx)
{
  PetscErrorCode ierr;
  Userctx        *user=(Userctx*)ctx;
  PetscScalar    *f;
  PetscInt       i;

  PetscFunctionBegin;
  ierr = ResidualFunctionPassive(snes,X,F,user);CHKERRQ(ierr);
  ierr = VecGetArray(F,&f);CHKERRQ(ierr);
  for (i=0; i < ngen; i++) {
    f[9*i]   = 0;
    f[9*i+1] = 0;
    f[9*i+2] = 0;
    f[9*i+3] = 0;
    f[9*i+6] = 0;
    f[9*i+7] = 0;
    f[9*i+8] = 0;
  }
  ierr = VecRestoreArray(F,&f);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

