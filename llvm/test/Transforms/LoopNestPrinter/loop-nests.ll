; RUN: opt -S -passes='loop-nest(print)' < %s 2>&1 >/dev/null | FileCheck %s

; CHECK: IsPerfect=true, Depth=1, OutermostLoop: for.cond, Loops: ( for.cond )
define i32 @f1(i32 %n) #0 {
entry:
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %entry
  %res.0 = phi i32 [ 0, %entry ], [ %add, %for.inc ]
  %i.0 = phi i32 [ 0, %entry ], [ %inc, %for.inc ]
  %cmp = icmp slt i32 %i.0, %n
  br i1 %cmp, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %add = add nsw i32 %res.0, %i.0
  br label %for.inc

for.inc:                                          ; preds = %for.body
  %inc = add nsw i32 %i.0, 1
  br label %for.cond

for.end:                                          ; preds = %for.cond
  %res.0.lcssa = phi i32 [ %res.0, %for.cond ]
  ret i32 %res.0.lcssa
}


; CHECK: IsPerfect=true, Depth=2, OutermostLoop: for.cond, Loops: ( for.cond for.cond1 )
define i32 @f2(i32 %n) #0 {
entry:
  br label %for.cond

for.cond:                                         ; preds = %for.inc4, %entry
  %i.0 = phi i32 [ 0, %entry ], [ %inc5, %for.inc4 ]
  %res.0 = phi i32 [ 0, %entry ], [ %res.1.lcssa, %for.inc4 ]
  %cmp = icmp slt i32 %i.0, %n
  br i1 %cmp, label %for.body, label %for.end6

for.body:                                         ; preds = %for.cond
  br label %for.cond1

for.cond1:                                        ; preds = %for.inc, %for.body
  %res.1 = phi i32 [ %res.0, %for.body ], [ %add, %for.inc ]
  %j.0 = phi i32 [ 0, %for.body ], [ %inc, %for.inc ]
  %cmp2 = icmp slt i32 %j.0, %n
  br i1 %cmp2, label %for.body3, label %for.end

for.body3:                                        ; preds = %for.cond1
  %add = add nsw i32 %res.1, %i.0
  br label %for.inc

for.inc:                                          ; preds = %for.body3
  %inc = add nsw i32 %j.0, 1
  br label %for.cond1

for.end:                                          ; preds = %for.cond1
  %res.1.lcssa = phi i32 [ %res.1, %for.cond1 ]
  br label %for.inc4

for.inc4:                                         ; preds = %for.end
  %inc5 = add nsw i32 %i.0, 1
  br label %for.cond

for.end6:                                         ; preds = %for.cond
  %res.0.lcssa = phi i32 [ %res.0, %for.cond ]
  ret i32 %res.0.lcssa
}

