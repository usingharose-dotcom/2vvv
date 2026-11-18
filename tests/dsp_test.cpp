// Renders a test signal through YoloDSP and prints the output (compared against the JS version).
#include "../Source/YoloDSP.h"
#include <cstdio>
#include <cstdlib>
int main(int argc,char**argv){
  double sr=atof(argv[1]); int n=atoi(argv[2]);
  YoloParams p; p.speedMs=atof(argv[3]); p.amount=atof(argv[4]); p.sustain=atof(argv[5]); p.stab=atoi(argv[6]);
  p.doubler=atof(argv[7]); p.flex=atof(argv[8]); p.hum=atof(argv[9]); p.inst=atoi(argv[10]); p.lowLat=atoi(argv[11]);
  const char* mask=argv[12]; for(int i=0;i<12;i++) p.allowed[i]=mask[i]=='1';
  YoloDSP d; d.prepare(sr);
  std::vector<float> in(n),L(n),R(n);
  for(int i=0;i<n;i++){ double t=i/sr; double f=220*pow(2,(0.4*sin(t*1.3)+ t*0.2)/12.0); static double ph=0; ph+=f/sr; in[i]=0.4*sin(2*M_PI*ph)+0.1*sin(4*M_PI*ph);}
  for(int s=0;s<n;s+=128){int m=std::min(128,n-s); d.process(in.data()+s,L.data()+s,R.data()+s,m,p);}
  fwrite(L.data(),4,n,stdout); fwrite(R.data(),4,n,stdout);
}
