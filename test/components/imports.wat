;; Imports of every extern kind, and an instance type declaring exports.
(component
  (type $ft (func (param "n" u32) (result u32)))
  (import "plain-func" (func (type $ft)))
  (type $it (instance
    (export "inner" (func (type $ft)))
    (export "t" (type (sub resource)))))
  (import "an-instance" (instance (type $it)))
  (type $ct (component
    (import "needed" (func (type $ft)))
    (export "given" (func (type $ft)))))
  (import "a-component" (component (type $ct)))
  (import "a-type" (type (eq $ft)))
  (import "abstract" (type (sub resource)))
)
