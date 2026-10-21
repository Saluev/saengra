;;; The Waltz line labeling benchmark, CLIPS version, from the OPS5 benchmark suite.
;;;
;;; Copyright C 1991 Satoshi Nishiyama and the University of Texas at Austin
;;; anyone may copy and use this program the terms listed in the copyright
;;; notice in and covering the Communications of the ACM, the program is
;;; is not warranted to do anything
;;;
;;; Rules are the same as in the CLIPS distribution's version; changes made here:
;;;   - the `begin` rule with a hard-coded drawing is removed, the driver asserts lines
;;;     and the `duplicate` stage instead (see waltz/clips_engine.py);
;;;   - get-angle uses CLIPS's built-in atan2 (the original deffunctions had syntax slips),
;;;     and a missing parenthesis in label_arrow-3A is fixed.

(deftemplate stage (slot value))
(deftemplate line (slot p1) (slot p2))
(deftemplate edge (slot p1) (slot p2) (slot joined) (slot label) (slot plotted))
(deftemplate junction (slot p1) (slot p2) (slot p3) (slot base_point) (slot type))

(defglobal ?*MOD-NUM* = 100)

(deffunction get-y (?val) (mod ?val ?*MOD-NUM*))

(deffunction get-x (?val) (div ?val ?*MOD-NUM*))

(deffunction get-angle (?p1 ?p2)
   (atan2 (- (get-y ?p2) (get-y ?p1)) (- (get-x ?p2) (get-x ?p1))))

(deffunction inscribed-angle (?basepoint ?p1 ?p2)
   (bind ?temp (abs (- (get-angle ?basepoint ?p1) (get-angle ?basepoint ?p2))))
   (if (> ?temp (pi)) then (bind ?temp (- (* 2 (pi)) ?temp)))
   (return ?temp))

(deffunction make-3-junction (?basepoint ?p1 ?p2 ?p3)
   (bind ?angle12 (inscribed-angle ?basepoint ?p1 ?p2))
   (bind ?angle13 (inscribed-angle ?basepoint ?p1 ?p3))
   (bind ?angle23 (inscribed-angle ?basepoint ?p2 ?p3))
   (bind ?sum1213 (+ ?angle12 ?angle13))
   (bind ?sum1223 (+ ?angle12 ?angle23))
   (bind ?sum1323 (+ ?angle13 ?angle23))
   (if (< ?sum1213 ?sum1223)
      then
      (if (< ?sum1213 ?sum1323)
         then (bind ?sum ?sum1213) (bind ?shaft ?p1) (bind ?barb1 ?p2) (bind ?barb2 ?p3)
         else (bind ?sum ?sum1323) (bind ?shaft ?p3) (bind ?barb1 ?p1) (bind ?barb2 ?p2))
      else
      (if (< ?sum1223 ?sum1323)
         then (bind ?sum ?sum1223) (bind ?shaft ?p2) (bind ?barb1 ?p1) (bind ?barb2 ?p3)
         else (bind ?sum ?sum1323) (bind ?shaft ?p3) (bind ?barb1 ?p1) (bind ?barb2 ?p2)))
   (if (< (abs (- ?sum (pi))) 0.001)
      then (bind ?type tee)
      else (if (> ?sum (pi)) then (bind ?type fork) else (bind ?type arrow)))
   (assert (junction (p1 ?barb1) (p2 ?shaft) (p3 ?barb2) (base_point ?basepoint) (type ?type))))

;;; Duplicating lines into edges

(defrule reverse_edges
   (stage (value duplicate))
   ?f2 <- (line (p1 ?p1) (p2 ?p2))
   =>
   (assert (edge (p1 ?p1) (p2 ?p2) (joined false)))
   (assert (edge (p1 ?p2) (p2 ?p1) (joined false)))
   (retract ?f2))

(defrule done_reversing
   (declare (salience -10))
   ?f1 <- (stage (value duplicate))
   (not (line))
   =>
   (modify ?f1 (value detect_junctions)))

;;; Detecting junctions

(defrule make-3_junction
   (declare (salience 10))
   (stage (value detect_junctions))
   ?f2 <- (edge (p1 ?base_point) (p2 ?p1) (joined false))
   ?f3 <- (edge (p1 ?base_point) (p2 ?p2&~?p1) (joined false))
   ?f4 <- (edge (p1 ?base_point) (p2 ?p3&~?p1&~?p2) (joined false))
   =>
   (make-3-junction ?base_point ?p1 ?p2 ?p3)
   (modify ?f2 (joined true))
   (modify ?f3 (joined true))
   (modify ?f4 (joined true)))

(defrule make_L
   (stage (value detect_junctions))
   ?f2 <- (edge (p1 ?base_point) (p2 ?p2) (joined false))
   ?f3 <- (edge (p1 ?base_point) (p2 ?p3&~?p2) (joined false))
   (not (edge (p1 ?base_point) (p2 ~?p2&~?p3)))
   =>
   (assert (junction (type L) (base_point ?base_point) (p1 ?p2) (p2 ?p3)))
   (modify ?f2 (joined true))
   (modify ?f3 (joined true)))

(defrule done_detecting
   (declare (salience -10))
   ?f1 <- (stage (value detect_junctions))
   (not (edge (joined false)))
   =>
   (modify ?f1 (value find_initial_boundary)))

;;; Labeling the boundary: the junctions with the largest and the smallest base points

(defrule initial_boundary_junction_L
   ?f1 <- (stage (value find_initial_boundary))
   (junction (type L) (base_point ?base_point) (p1 ?p1) (p2 ?p2))
   ?f3 <- (edge (p1 ?base_point) (p2 ?p1))
   ?f4 <- (edge (p1 ?base_point) (p2 ?p2))
   (not (junction (base_point ?bp&:(> ?bp ?base_point))))
   =>
   (modify ?f3 (label B))
   (modify ?f4 (label B))
   (modify ?f1 (value find_second_boundary)))

(defrule initial_boundary_junction_arrow
   ?f1 <- (stage (value find_initial_boundary))
   (junction (type arrow) (base_point ?bp) (p1 ?p1) (p2 ?p2) (p3 ?p3))
   ?f3 <- (edge (p1 ?bp) (p2 ?p1))
   ?f4 <- (edge (p1 ?bp) (p2 ?p2))
   ?f5 <- (edge (p1 ?bp) (p2 ?p3))
   (not (junction (base_point ?base_point&:(> ?base_point ?bp))))
   =>
   (modify ?f3 (label B))
   (modify ?f4 (label +))
   (modify ?f5 (label B))
   (modify ?f1 (value find_second_boundary)))

(defrule second_boundary_junction_L
   ?f1 <- (stage (value find_second_boundary))
   (junction (type L) (base_point ?base_point) (p1 ?p1) (p2 ?p2))
   ?f3 <- (edge (p1 ?base_point) (p2 ?p1))
   ?f4 <- (edge (p1 ?base_point) (p2 ?p2))
   (not (junction (base_point ?bp&:(< ?bp ?base_point))))
   =>
   (modify ?f3 (label B))
   (modify ?f4 (label B))
   (modify ?f1 (value labeling)))

(defrule second_boundary_junction_arrow
   ?f1 <- (stage (value find_second_boundary))
   (junction (type arrow) (base_point ?bp) (p1 ?p1) (p2 ?p2) (p3 ?p3))
   ?f3 <- (edge (p1 ?bp) (p2 ?p1))
   ?f4 <- (edge (p1 ?bp) (p2 ?p2))
   ?f5 <- (edge (p1 ?bp) (p2 ?p3))
   (not (junction (base_point ?base_point&:(< ?base_point ?bp))))
   =>
   (modify ?f3 (label B))
   (modify ?f4 (label +))
   (modify ?f5 (label B))
   (modify ?f1 (value labeling)))

;;; Propagating labels

(defrule match_edge
   (stage (value labeling))
   ?f2 <- (edge (p1 ?p1) (p2 ?p2) (label ?label& + | - | B))
   ?f3 <- (edge (p1 ?p2) (p2 ?p1) (label nil))
   =>
   (modify ?f2 (plotted t))
   (modify ?f3 (label ?label) (plotted t)))

(defrule label_L
   (stage (value labeling))
   (junction (type L) (base_point ?p1))
   (edge (p1 ?p1) (p2 ?p2) (label + | -))
   ?f4 <- (edge (p1 ?p1) (p2 ~?p2) (label nil))
   =>
   (modify ?f4 (label B)))

(defrule label_tee_A
   (declare (salience 5))
   (stage (value labeling))
   (junction (type tee) (base_point ?bp) (p1 ?p1) (p2 ?p2) (p3 ?p3))
   ?f3 <- (edge (p1 ?bp) (p2 ?p1) (label nil))
   ?f4 <- (edge (p1 ?bp) (p2 ?p3))
   =>
   (modify ?f3 (label B))
   (modify ?f4 (label B)))

(defrule label_tee_B
   (stage (value labeling))
   (junction (type tee) (base_point ?bp) (p1 ?p1) (p2 ?p2) (p3 ?p3))
   ?f3 <- (edge (p1 ?bp) (p2 ?p1))
   ?f4 <- (edge (p1 ?bp) (p2 ?p3) (label nil))
   =>
   (modify ?f3 (label B))
   (modify ?f4 (label B)))

(defrule label_fork-1
   (stage (value labeling))
   (junction (type fork) (base_point ?bp))
   (edge (p1 ?bp) (p2 ?p1) (label +))
   ?f4 <- (edge (p1 ?bp) (p2 ?p2&~?p1) (label nil))
   ?f5 <- (edge (p1 ?bp) (p2 ~?p2&~?p1))
   =>
   (modify ?f4 (label +))
   (modify ?f5 (label +)))

(defrule label_fork-2
   (stage (value labeling))
   (junction (type fork) (base_point ?bp))
   (edge (p1 ?bp) (p2 ?p1) (label B))
   (edge (p1 ?bp) (p2 ?p2&~?p1) (label -))
   ?f5 <- (edge (p1 ?bp) (p2 ~?p2&~?p1) (label nil))
   =>
   (modify ?f5 (label B)))

(defrule label_fork-3
   (stage (value labeling))
   (junction (type fork) (base_point ?bp))
   (edge (p1 ?bp) (p2 ?p1) (label B))
   (edge (p1 ?bp) (p2 ?p2&~?p1) (label B))
   ?f5 <- (edge (p1 ?bp) (p2 ~?p2&~?p1) (label nil))
   =>
   (modify ?f5 (label -)))

(defrule label_fork-4
   (stage (value labeling))
   (junction (type fork) (base_point ?bp))
   (edge (p1 ?bp) (p2 ?p1) (label -))
   (edge (p1 ?bp) (p2 ?p2&~?p1) (label -))
   ?f5 <- (edge (p1 ?bp) (p2 ~?p2&~?p1) (label nil))
   =>
   (modify ?f5 (label -)))

(defrule label_arrow-1A
   (declare (salience 5))
   (stage (value labeling))
   (junction (type arrow) (base_point ?bp) (p1 ?p1) (p2 ?p2) (p3 ?p3))
   (edge (p1 ?bp) (p2 ?p1) (label ?label& B | -))
   ?f4 <- (edge (p1 ?bp) (p2 ?p2) (label nil))
   ?f5 <- (edge (p1 ?bp) (p2 ?p3))
   =>
   (modify ?f4 (label +))
   (modify ?f5 (label ?label)))

(defrule label_arrow-1B
   (stage (value labeling))
   (junction (type arrow) (base_point ?bp) (p1 ?p1) (p2 ?p2) (p3 ?p3))
   (edge (p1 ?bp) (p2 ?p1) (label ?label& B | -))
   ?f4 <- (edge (p1 ?bp) (p2 ?p2))
   ?f5 <- (edge (p1 ?bp) (p2 ?p3) (label nil))
   =>
   (modify ?f4 (label +))
   (modify ?f5 (label ?label)))

(defrule label_arrow-2A
   (declare (salience 5))
   (stage (value labeling))
   (junction (type arrow) (base_point ?bp) (p1 ?p1) (p2 ?p2) (p3 ?p3))
   (edge (p1 ?bp) (p2 ?p3) (label ?label& B | -))
   ?f4 <- (edge (p1 ?bp) (p2 ?p2) (label nil))
   ?f5 <- (edge (p1 ?bp) (p2 ?p1))
   =>
   (modify ?f4 (label +))
   (modify ?f5 (label ?label)))

(defrule label_arrow-2B
   (stage (value labeling))
   (junction (type arrow) (base_point ?bp) (p1 ?p1) (p2 ?p2) (p3 ?p3))
   (edge (p1 ?bp) (p2 ?p3) (label ?label& B | -))
   ?f4 <- (edge (p1 ?bp) (p2 ?p2))
   ?f5 <- (edge (p1 ?bp) (p2 ?p1) (label nil))
   =>
   (modify ?f4 (label +))
   (modify ?f5 (label ?label)))

(defrule label_arrow-3A
   (declare (salience 5))
   (stage (value labeling))
   (junction (type arrow) (base_point ?bp) (p1 ?p1) (p2 ?p2) (p3 ?p3))
   (edge (p1 ?bp) (p2 ?p1) (label +))
   ?f4 <- (edge (p1 ?bp) (p2 ?p2) (label nil))
   ?f5 <- (edge (p1 ?bp) (p2 ?p3))
   =>
   (modify ?f4 (label -))
   (modify ?f5 (label +)))

(defrule label_arrow-3B
   (stage (value labeling))
   (junction (type arrow) (base_point ?bp) (p1 ?p1) (p2 ?p2) (p3 ?p3))
   (edge (p1 ?bp) (p2 ?p1) (label +))
   ?f4 <- (edge (p1 ?bp) (p2 ?p2))
   ?f5 <- (edge (p1 ?bp) (p2 ?p3) (label nil))
   =>
   (modify ?f4 (label -))
   (modify ?f5 (label +)))

(defrule label_arrow-4A
   (declare (salience 5))
   (stage (value labeling))
   (junction (type arrow) (base_point ?bp) (p1 ?p1) (p2 ?p2) (p3 ?p3))
   (edge (p1 ?bp) (p2 ?p3) (label +))
   ?f4 <- (edge (p1 ?bp) (p2 ?p2) (label nil))
   ?f5 <- (edge (p1 ?bp) (p2 ?p1))
   =>
   (modify ?f4 (label -))
   (modify ?f5 (label +)))

(defrule label_arrow-4B
   (stage (value labeling))
   (junction (type arrow) (base_point ?bp) (p1 ?p1) (p2 ?p2) (p3 ?p3))
   (edge (p1 ?bp) (p2 ?p3) (label +))
   ?f4 <- (edge (p1 ?bp) (p2 ?p2))
   ?f5 <- (edge (p1 ?bp) (p2 ?p1) (label nil))
   =>
   (modify ?f4 (label -))
   (modify ?f5 (label +)))

(defrule label_arrow-5A
   (declare (salience 5))
   (stage (value labeling))
   (junction (type arrow) (base_point ?bp) (p1 ?p1) (p2 ?p2) (p3 ?p3))
   (edge (p1 ?bp) (p2 ?p2) (label -))
   ?f4 <- (edge (p1 ?bp) (p2 ?p1))
   ?f5 <- (edge (p1 ?bp) (p2 ?p3) (label nil))
   =>
   (modify ?f4 (label +))
   (modify ?f5 (label +)))

(defrule label_arrow-5B
   (stage (value labeling))
   (junction (type arrow) (base_point ?bp) (p1 ?p1) (p2 ?p2) (p3 ?p3))
   (edge (p1 ?bp) (p2 ?p2) (label -))
   ?f4 <- (edge (p1 ?bp) (p2 ?p1) (label nil))
   ?f5 <- (edge (p1 ?bp) (p2 ?p3))
   =>
   (modify ?f4 (label +))
   (modify ?f5 (label +)))

(defrule done_labeling
   (declare (salience -10))
   ?f1 <- (stage (value labeling))
   =>
   (modify ?f1 (value plot_remaining_edges)))

;;; Plotting

(defrule plot_remaining
   (stage (value plot_remaining_edges))
   ?f2 <- (edge (plotted nil) (label ?label&~nil) (p1 ?p1) (p2 ?p2))
   =>
   (modify ?f2 (plotted t)))

(defrule plot_boundaries
   (stage (value plot_remaining_edges))
   ?f2 <- (edge (plotted nil) (label nil) (p1 ?p1) (p2 ?p2))
   =>
   (modify ?f2 (plotted t)))

(defrule done_plotting
   (declare (salience -10))
   ?f1 <- (stage (value plot_remaining_edges))
   (not (edge (plotted nil)))
   =>
   (modify ?f1 (value done)))
