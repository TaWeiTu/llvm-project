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
  %cmp4 = icmp slt i32 0, %n
  br i1 %cmp4, label %for.body.lr.ph, label %for.end6

for.body.lr.ph:                                   ; preds = %entry
  br label %for.body

for.body:                                         ; preds = %for.body.lr.ph, %for.inc4
  %res.06 = phi i32 [ 0, %for.body.lr.ph ], [ %res.1.lcssa, %for.inc4 ]
  %i.05 = phi i32 [ 0, %for.body.lr.ph ], [ %inc5, %for.inc4 ]
  %cmp21 = icmp slt i32 0, %n
  br i1 %cmp21, label %for.body3.lr.ph, label %for.end

for.body3.lr.ph:                                  ; preds = %for.body
  br label %for.body3

for.body3:                                        ; preds = %for.body3.lr.ph, %for.inc
  %j.03 = phi i32 [ 0, %for.body3.lr.ph ], [ %inc, %for.inc ]
  %res.12 = phi i32 [ %res.06, %for.body3.lr.ph ], [ %add, %for.inc ]
  %add = add nsw i32 %res.12, %i.05
  br label %for.inc

for.inc:                                          ; preds = %for.body3
  %inc = add nsw i32 %j.03, 1
  %cmp2 = icmp slt i32 %inc, %n
  br i1 %cmp2, label %for.body3, label %for.cond1.for.end_crit_edge

for.cond1.for.end_crit_edge:                      ; preds = %for.inc
  %split = phi i32 [ %add, %for.inc ]
  br label %for.end

for.end:                                          ; preds = %for.cond1.for.end_crit_edge, %for.body
  %res.1.lcssa = phi i32 [ %split, %for.cond1.for.end_crit_edge ], [ %res.06, %for.body ]
  br label %for.inc4

for.inc4:                                         ; preds = %for.end
  %inc5 = add nsw i32 %i.05, 1
  %cmp = icmp slt i32 %inc5, %n
  br i1 %cmp, label %for.body, label %for.cond.for.end6_crit_edge

for.cond.for.end6_crit_edge:                      ; preds = %for.inc4
  %split7 = phi i32 [ %res.1.lcssa, %for.inc4 ]
  br label %for.end6

for.end6:                                         ; preds = %for.cond.for.end6_crit_edge, %entry
  %res.0.lcssa = phi i32 [ %split7, %for.cond.for.end6_crit_edge ], [ 0, %entry ]
  ret i32 %res.0.lcssa
}

