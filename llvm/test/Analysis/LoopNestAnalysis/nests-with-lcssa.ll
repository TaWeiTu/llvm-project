; RUN: opt -S -passes='print<loopnest>' < %s 2>&1 > /dev/null | FileCheck %s

; int f(int N, int M) {
;   int res = 0;
;   for (int i = 0; i < N; ++i) {
;     for (int j = 0; j < M; ++j) res += i * j;
;   }
;   return res;
; }

define i32 @f(i32 %N, i32 %M) #0 {
; CHECK: IsPerfect=true, Depth=1, OutermostLoop: for.body3, Loops: ( for.body3 )
; CHECK: IsPerfect=true, Depth=2, OutermostLoop: for.body, Loops: ( for.body for.body3 )
entry:
  %cmp4 = icmp slt i32 0, %N
  br i1 %cmp4, label %for.body.lr.ph, label %for.end6

for.body.lr.ph:                                   ; preds = %entry
  br label %for.body

for.body:                                         ; preds = %for.body.lr.ph, %for.inc4
  %i.06 = phi i32 [ 0, %for.body.lr.ph ], [ %inc5, %for.inc4 ]
  %res.05 = phi i32 [ 0, %for.body.lr.ph ], [ %res.1.lcssa, %for.inc4 ]
  %cmp21 = icmp slt i32 0, %M
  br i1 %cmp21, label %for.body3.lr.ph, label %for.end

for.body3.lr.ph:                                  ; preds = %for.body
  br label %for.body3

for.body3:                                        ; preds = %for.body3.lr.ph, %for.inc
  %j.03 = phi i32 [ 0, %for.body3.lr.ph ], [ %inc, %for.inc ]
  %res.12 = phi i32 [ %res.05, %for.body3.lr.ph ], [ %add, %for.inc ]
  %mul = mul nsw i32 %i.06, %j.03
  %add = add nsw i32 %res.12, %mul
  br label %for.inc

for.inc:                                          ; preds = %for.body3
  %inc = add nsw i32 %j.03, 1
  %cmp2 = icmp slt i32 %inc, %M
  br i1 %cmp2, label %for.body3, label %for.cond1.for.end_crit_edge

for.cond1.for.end_crit_edge:                      ; preds = %for.inc
  %split = phi i32 [ %add, %for.inc ]
  br label %for.end

for.end:                                          ; preds = %for.cond1.for.end_crit_edge, %for.body
  %res.1.lcssa = phi i32 [ %split, %for.cond1.for.end_crit_edge ], [ %res.05, %for.body ]
  br label %for.inc4

for.inc4:                                         ; preds = %for.end
  %inc5 = add nsw i32 %i.06, 1
  %cmp = icmp slt i32 %inc5, %N
  br i1 %cmp, label %for.body, label %for.cond.for.end6_crit_edge

for.cond.for.end6_crit_edge:                      ; preds = %for.inc4
  %split7 = phi i32 [ %res.1.lcssa, %for.inc4 ]
  br label %for.end6

for.end6:                                         ; preds = %for.cond.for.end6_crit_edge, %entry
  %res.0.lcssa = phi i32 [ %split7, %for.cond.for.end6_crit_edge ], [ 0, %entry ]
  ret i32 %res.0.lcssa
}

; int g(int N, int M, int K) {
;   int sum = 0, prod = 1;
;   for (int i = 0; i < N; ++i) {
;     for (int j = 0; j < M; ++j) {
;       for (int k = 0; k < K; ++k) {
;         sum += i * j * k;
;       }
;       prod *= (i + j);
;     }
;   }
;   return sum + prod;
; }
define i32 @g(i32 %N, i32 %M, i32 %K) #0 {
; CHECK: IsPerfect=true, Depth=1, OutermostLoop: for.body6, Loops: ( for.body6 )
; CHECK: IsPerfect=false, Depth=2, OutermostLoop: for.body3, Loops: ( for.body3 for.body6 )
; CHECK: IsPerfect=false, Depth=3, OutermostLoop: for.body, Loops: ( for.body for.body3 for.body6 )
entry:
  %cmp10 = icmp slt i32 0, %N
  br i1 %cmp10, label %for.body.lr.ph, label %for.end15

for.body.lr.ph:                                   ; preds = %entry
  br label %for.body

for.body:                                         ; preds = %for.body.lr.ph, %for.inc13
  %i.013 = phi i32 [ 0, %for.body.lr.ph ], [ %inc14, %for.inc13 ]
  %sum.012 = phi i32 [ 0, %for.body.lr.ph ], [ %sum.1.lcssa, %for.inc13 ]
  %prod.011 = phi i32 [ 1, %for.body.lr.ph ], [ %prod.1.lcssa, %for.inc13 ]
  %cmp24 = icmp slt i32 0, %M
  br i1 %cmp24, label %for.body3.lr.ph, label %for.end12

for.body3.lr.ph:                                  ; preds = %for.body
  br label %for.body3

for.body3:                                        ; preds = %for.body3.lr.ph, %for.inc10
  %j.07 = phi i32 [ 0, %for.body3.lr.ph ], [ %inc11, %for.inc10 ]
  %sum.16 = phi i32 [ %sum.012, %for.body3.lr.ph ], [ %sum.2.lcssa, %for.inc10 ]
  %prod.15 = phi i32 [ %prod.011, %for.body3.lr.ph ], [ %mul9, %for.inc10 ]
  %cmp51 = icmp slt i32 0, %K
  br i1 %cmp51, label %for.body6.lr.ph, label %for.end

for.body6.lr.ph:                                  ; preds = %for.body3
  br label %for.body6

for.body6:                                        ; preds = %for.body6.lr.ph, %for.inc
  %k.03 = phi i32 [ 0, %for.body6.lr.ph ], [ %inc, %for.inc ]
  %sum.22 = phi i32 [ %sum.16, %for.body6.lr.ph ], [ %add, %for.inc ]
  %mul = mul nsw i32 %i.013, %j.07
  %mul7 = mul nsw i32 %mul, %k.03
  %add = add nsw i32 %sum.22, %mul7
  br label %for.inc

for.inc:                                          ; preds = %for.body6
  %inc = add nsw i32 %k.03, 1
  %cmp5 = icmp slt i32 %inc, %K
  br i1 %cmp5, label %for.body6, label %for.cond4.for.end_crit_edge

for.cond4.for.end_crit_edge:                      ; preds = %for.inc
  %split = phi i32 [ %add, %for.inc ]
  br label %for.end

for.end:                                          ; preds = %for.cond4.for.end_crit_edge, %for.body3
  %sum.2.lcssa = phi i32 [ %split, %for.cond4.for.end_crit_edge ], [ %sum.16, %for.body3 ]
  %add8 = add nsw i32 %i.013, %j.07
  %mul9 = mul nsw i32 %prod.15, %add8
  br label %for.inc10

for.inc10:                                        ; preds = %for.end
  %inc11 = add nsw i32 %j.07, 1
  %cmp2 = icmp slt i32 %inc11, %M
  br i1 %cmp2, label %for.body3, label %for.cond1.for.end12_crit_edge

for.cond1.for.end12_crit_edge:                    ; preds = %for.inc10
  %split8 = phi i32 [ %mul9, %for.inc10 ]
  %split9 = phi i32 [ %sum.2.lcssa, %for.inc10 ]
  br label %for.end12

for.end12:                                        ; preds = %for.cond1.for.end12_crit_edge, %for.body
  %prod.1.lcssa = phi i32 [ %split8, %for.cond1.for.end12_crit_edge ], [ %prod.011, %for.body ]
  %sum.1.lcssa = phi i32 [ %split9, %for.cond1.for.end12_crit_edge ], [ %sum.012, %for.body ]
  br label %for.inc13

for.inc13:                                        ; preds = %for.end12
  %inc14 = add nsw i32 %i.013, 1
  %cmp = icmp slt i32 %inc14, %N
  br i1 %cmp, label %for.body, label %for.cond.for.end15_crit_edge

for.cond.for.end15_crit_edge:                     ; preds = %for.inc13
  %split14 = phi i32 [ %prod.1.lcssa, %for.inc13 ]
  %split15 = phi i32 [ %sum.1.lcssa, %for.inc13 ]
  br label %for.end15

for.end15:                                        ; preds = %for.cond.for.end15_crit_edge, %entry
  %prod.0.lcssa = phi i32 [ %split14, %for.cond.for.end15_crit_edge ], [ 1, %entry ]
  %sum.0.lcssa = phi i32 [ %split15, %for.cond.for.end15_crit_edge ], [ 0, %entry ]
  %add16 = add nsw i32 %sum.0.lcssa, %prod.0.lcssa
  ret i32 %add16
}

; int h(int N, int M, int K) {
;   int sum = 0;
;   for (int i = 0; i < N; ++i) {
;     for (int j = 0; j < M; ++j) {
;       for (int k = 0; k < K; ++k) {
;         sum += i * j * k;
;       }
;     }
;   }
;   return sum;
; }
define i32 @h(i32 %N, i32 %M, i32 %K) #0 {
; CHECK: IsPerfect=true, Depth=1, OutermostLoop: for.body6, Loops: ( for.body6 )
; CHECK: IsPerfect=true, Depth=2, OutermostLoop: for.body3, Loops: ( for.body3 for.body6 )
; CHECK: IsPerfect=true, Depth=3, OutermostLoop: for.body, Loops: ( for.body for.body3 for.body6 )
entry:
  %cmp8 = icmp slt i32 0, %N
  br i1 %cmp8, label %for.body.lr.ph, label %for.end13

for.body.lr.ph:                                   ; preds = %entry
  br label %for.body

for.body:                                         ; preds = %for.body.lr.ph, %for.inc11
  %i.010 = phi i32 [ 0, %for.body.lr.ph ], [ %inc12, %for.inc11 ]
  %sum.09 = phi i32 [ 0, %for.body.lr.ph ], [ %sum.1.lcssa, %for.inc11 ]
  %cmp24 = icmp slt i32 0, %M
  br i1 %cmp24, label %for.body3.lr.ph, label %for.end10

for.body3.lr.ph:                                  ; preds = %for.body
  br label %for.body3

for.body3:                                        ; preds = %for.body3.lr.ph, %for.inc8
  %j.06 = phi i32 [ 0, %for.body3.lr.ph ], [ %inc9, %for.inc8 ]
  %sum.15 = phi i32 [ %sum.09, %for.body3.lr.ph ], [ %sum.2.lcssa, %for.inc8 ]
  %cmp51 = icmp slt i32 0, %K
  br i1 %cmp51, label %for.body6.lr.ph, label %for.end

for.body6.lr.ph:                                  ; preds = %for.body3
  br label %for.body6

for.body6:                                        ; preds = %for.body6.lr.ph, %for.inc
  %k.03 = phi i32 [ 0, %for.body6.lr.ph ], [ %inc, %for.inc ]
  %sum.22 = phi i32 [ %sum.15, %for.body6.lr.ph ], [ %add, %for.inc ]
  %mul = mul nsw i32 %i.010, %j.06
  %mul7 = mul nsw i32 %mul, %k.03
  %add = add nsw i32 %sum.22, %mul7
  br label %for.inc

for.inc:                                          ; preds = %for.body6
  %inc = add nsw i32 %k.03, 1
  %cmp5 = icmp slt i32 %inc, %K
  br i1 %cmp5, label %for.body6, label %for.cond4.for.end_crit_edge

for.cond4.for.end_crit_edge:                      ; preds = %for.inc
  %split = phi i32 [ %add, %for.inc ]
  br label %for.end

for.end:                                          ; preds = %for.cond4.for.end_crit_edge, %for.body3
  %sum.2.lcssa = phi i32 [ %split, %for.cond4.for.end_crit_edge ], [ %sum.15, %for.body3 ]
  br label %for.inc8

for.inc8:                                         ; preds = %for.end
  %inc9 = add nsw i32 %j.06, 1
  %cmp2 = icmp slt i32 %inc9, %M
  br i1 %cmp2, label %for.body3, label %for.cond1.for.end10_crit_edge

for.cond1.for.end10_crit_edge:                    ; preds = %for.inc8
  %split7 = phi i32 [ %sum.2.lcssa, %for.inc8 ]
  br label %for.end10

for.end10:                                        ; preds = %for.cond1.for.end10_crit_edge, %for.body
  %sum.1.lcssa = phi i32 [ %split7, %for.cond1.for.end10_crit_edge ], [ %sum.09, %for.body ]
  br label %for.inc11

for.inc11:                                        ; preds = %for.end10
  %inc12 = add nsw i32 %i.010, 1
  %cmp = icmp slt i32 %inc12, %N
  br i1 %cmp, label %for.body, label %for.cond.for.end13_crit_edge

for.cond.for.end13_crit_edge:                     ; preds = %for.inc11
  %split11 = phi i32 [ %sum.1.lcssa, %for.inc11 ]
  br label %for.end13

for.end13:                                        ; preds = %for.cond.for.end13_crit_edge, %entry
  %sum.0.lcssa = phi i32 [ %split11, %for.cond.for.end13_crit_edge ], [ 0, %entry ]
  ret i32 %sum.0.lcssa
}
