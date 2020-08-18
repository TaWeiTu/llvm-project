; RUN: opt -S -passes='loop-rotate,print<loopnest>' < %s 2>&1 > /dev/null | FileCheck %s
; Function Attrs: noinline nounwind optnone
; CHECK: IsPerfect=true, Depth=1, OutermostLoop: for.body3, Loops: ( for.body3 )
; CHECK: IsPerfect=true, Depth=2, OutermostLoop: for.body, Loops: ( for.body for.body3 )
define i32 @f(i32 %N, i32 %M) #0 {
entry:
  br label %for.cond

for.cond:                                         ; preds = %for.inc4, %entry
  %res.0 = phi i32 [ 0, %entry ], [ %res.1, %for.inc4 ]
  %i.0 = phi i32 [ 0, %entry ], [ %inc5, %for.inc4 ]
  %cmp = icmp slt i32 %i.0, %N
  br i1 %cmp, label %for.body, label %for.end6

for.body:                                         ; preds = %for.cond
  br label %for.cond1

for.cond1:                                        ; preds = %for.inc, %for.body
  %res.1 = phi i32 [ %res.0, %for.body ], [ %add, %for.inc ]
  %j.0 = phi i32 [ 0, %for.body ], [ %inc, %for.inc ]
  %cmp2 = icmp slt i32 %j.0, %M
  br i1 %cmp2, label %for.body3, label %for.end

for.body3:                                        ; preds = %for.cond1
  %mul = mul nsw i32 %i.0, %j.0
  %add = add nsw i32 %res.1, %mul
  br label %for.inc

for.inc:                                          ; preds = %for.body3
  %inc = add nsw i32 %j.0, 1
  br label %for.cond1

for.end:                                          ; preds = %for.cond1
  br label %for.inc4

for.inc4:                                         ; preds = %for.end
  %inc5 = add nsw i32 %i.0, 1
  br label %for.cond

for.end6:                                         ; preds = %for.cond
  ret i32 %res.0
}

; Function Attrs: noinline nounwind optnone
; CHECK: IsPerfect=true, Depth=1, OutermostLoop: for.body6, Loops: ( for.body6 )
; CHECK: IsPerfect=false, Depth=2, OutermostLoop: for.body3, Loops: ( for.body3 for.body6 )
; CHECK: IsPerfect=false, Depth=3, OutermostLoop: for.body, Loops: ( for.body for.body3 for.body6 )
define i32 @g(i32 %N, i32 %M, i32 %K) #0 {
entry:
  br label %for.cond

for.cond:                                         ; preds = %for.inc13, %entry
  %prod.0 = phi i32 [ 1, %entry ], [ %prod.1, %for.inc13 ]
  %sum.0 = phi i32 [ 0, %entry ], [ %sum.1, %for.inc13 ]
  %i.0 = phi i32 [ 0, %entry ], [ %inc14, %for.inc13 ]
  %cmp = icmp slt i32 %i.0, %N
  br i1 %cmp, label %for.body, label %for.end15

for.body:                                         ; preds = %for.cond
  br label %for.cond1

for.cond1:                                        ; preds = %for.inc10, %for.body
  %prod.1 = phi i32 [ %prod.0, %for.body ], [ %mul9, %for.inc10 ]
  %sum.1 = phi i32 [ %sum.0, %for.body ], [ %sum.2, %for.inc10 ]
  %j.0 = phi i32 [ 0, %for.body ], [ %inc11, %for.inc10 ]
  %cmp2 = icmp slt i32 %j.0, %M
  br i1 %cmp2, label %for.body3, label %for.end12

for.body3:                                        ; preds = %for.cond1
  br label %for.cond4

for.cond4:                                        ; preds = %for.inc, %for.body3
  %sum.2 = phi i32 [ %sum.1, %for.body3 ], [ %add, %for.inc ]
  %k.0 = phi i32 [ 0, %for.body3 ], [ %inc, %for.inc ]
  %cmp5 = icmp slt i32 %k.0, %K
  br i1 %cmp5, label %for.body6, label %for.end

for.body6:                                        ; preds = %for.cond4
  %mul = mul nsw i32 %i.0, %j.0
  %mul7 = mul nsw i32 %mul, %k.0
  %add = add nsw i32 %sum.2, %mul7
  br label %for.inc

for.inc:                                          ; preds = %for.body6
  %inc = add nsw i32 %k.0, 1
  br label %for.cond4

for.end:                                          ; preds = %for.cond4
  %add8 = add nsw i32 %i.0, %j.0
  %mul9 = mul nsw i32 %prod.1, %add8
  br label %for.inc10

for.inc10:                                        ; preds = %for.end
  %inc11 = add nsw i32 %j.0, 1
  br label %for.cond1

for.end12:                                        ; preds = %for.cond1
  br label %for.inc13

for.inc13:                                        ; preds = %for.end12
  %inc14 = add nsw i32 %i.0, 1
  br label %for.cond

for.end15:                                        ; preds = %for.cond
  %add16 = add nsw i32 %sum.0, %prod.0
  ret i32 %add16
}

; Function Attrs: noinline nounwind optnone
; CHECK: IsPerfect=true, Depth=1, OutermostLoop: for.body6, Loops: ( for.body6 )
; CHECK: IsPerfect=true, Depth=2, OutermostLoop: for.body3, Loops: ( for.body3 for.body6 )
; CHECK: IsPerfect=true, Depth=3, OutermostLoop: for.body, Loops: ( for.body for.body3 for.body6 )
define i32 @h(i32 %N, i32 %M, i32 %K) #0 {
entry:
  br label %for.cond

for.cond:                                         ; preds = %for.inc11, %entry
  %sum.0 = phi i32 [ 0, %entry ], [ %sum.1, %for.inc11 ]
  %i.0 = phi i32 [ 0, %entry ], [ %inc12, %for.inc11 ]
  %cmp = icmp slt i32 %i.0, %N
  br i1 %cmp, label %for.body, label %for.end13

for.body:                                         ; preds = %for.cond
  br label %for.cond1

for.cond1:                                        ; preds = %for.inc8, %for.body
  %sum.1 = phi i32 [ %sum.0, %for.body ], [ %sum.2, %for.inc8 ]
  %j.0 = phi i32 [ 0, %for.body ], [ %inc9, %for.inc8 ]
  %cmp2 = icmp slt i32 %j.0, %M
  br i1 %cmp2, label %for.body3, label %for.end10

for.body3:                                        ; preds = %for.cond1
  br label %for.cond4

for.cond4:                                        ; preds = %for.inc, %for.body3
  %sum.2 = phi i32 [ %sum.1, %for.body3 ], [ %add, %for.inc ]
  %k.0 = phi i32 [ 0, %for.body3 ], [ %inc, %for.inc ]
  %cmp5 = icmp slt i32 %k.0, %K
  br i1 %cmp5, label %for.body6, label %for.end

for.body6:                                        ; preds = %for.cond4
  %mul = mul nsw i32 %i.0, %j.0
  %mul7 = mul nsw i32 %mul, %k.0
  %add = add nsw i32 %sum.2, %mul7
  br label %for.inc

for.inc:                                          ; preds = %for.body6
  %inc = add nsw i32 %k.0, 1
  br label %for.cond4

for.end:                                          ; preds = %for.cond4
  br label %for.inc8

for.inc8:                                         ; preds = %for.end
  %inc9 = add nsw i32 %j.0, 1
  br label %for.cond1

for.end10:                                        ; preds = %for.cond1
  br label %for.inc11

for.inc11:                                        ; preds = %for.end10
  %inc12 = add nsw i32 %i.0, 1
  br label %for.cond

for.end13:                                        ; preds = %for.cond
  ret i32 %sum.0
}
