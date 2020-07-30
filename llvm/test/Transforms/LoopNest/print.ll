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

; CHECH: IsPerfect=false, Depth=2, OutermostLoop: for.cond, Loops: ( for.cond for.cond1 for.cond5 )
define i32 @f4(i32 %n) #0 {
entry:
  br label %for.cond

for.cond:                                         ; preds = %for.inc12, %entry
  %i.0 = phi i32 [ 0, %entry ], [ %inc13, %for.inc12 ]
  %res.0 = phi i32 [ 0, %entry ], [ %res.2.lcssa, %for.inc12 ]
  %cmp = icmp slt i32 %i.0, %n
  br i1 %cmp, label %for.body, label %for.end14

for.body:                                         ; preds = %for.cond
  br label %for.cond1

for.cond1:                                        ; preds = %for.inc, %for.body
  %j.0 = phi i32 [ 0, %for.body ], [ %inc, %for.inc ]
  %res.1 = phi i32 [ %res.0, %for.body ], [ %add, %for.inc ]
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
  br label %for.cond5

for.cond5:                                        ; preds = %for.inc9, %for.end
  %res.2 = phi i32 [ %res.1.lcssa, %for.end ], [ %add8, %for.inc9 ]
  %j4.0 = phi i32 [ 0, %for.end ], [ %inc10, %for.inc9 ]
  %cmp6 = icmp slt i32 %j4.0, %n
  br i1 %cmp6, label %for.body7, label %for.end11

for.body7:                                        ; preds = %for.cond5
  %add8 = add nsw i32 %res.2, %j4.0
  br label %for.inc9

for.inc9:                                         ; preds = %for.body7
  %inc10 = add nsw i32 %j4.0, 1
  br label %for.cond5

for.end11:                                        ; preds = %for.cond5
  %res.2.lcssa = phi i32 [ %res.2, %for.cond5 ]
  br label %for.inc12

for.inc12:                                        ; preds = %for.end11
  %inc13 = add nsw i32 %i.0, 1
  br label %for.cond

for.end14:                                        ; preds = %for.cond
  %res.0.lcssa = phi i32 [ %res.0, %for.cond ]
  ret i32 %res.0.lcssa
}
