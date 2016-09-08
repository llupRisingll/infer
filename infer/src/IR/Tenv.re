/*
 * vim: set ft=rust:
 * vim: set ft=reason:
 *
 * Copyright (c) 2016 - present Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

open! Utils;


/** Module for Type Environments. */
/** Hash tables on strings. */
let module TypenameHash = Hashtbl.Make {
  type t = Typename.t;
  let equal tn1 tn2 => Typename.equal tn1 tn2;
  let hash = Hashtbl.hash;
};


/** Type for type environment. */
type t = TypenameHash.t Typ.struct_typ;


/** Create a new type environment. */
let create () => TypenameHash.create 1000;


/** Construct a struct type in a type environment */
let mk_struct
    tenv
    default::default=?
    fields::fields=?
    statics::statics=?
    methods::methods=?
    supers::supers=?
    annots::annots=?
    name => {
  let normalize_fields fs =>
    IList.map_changed
      (
        fun ((fld, typ, ann) as fta) =>
          switch typ {
          | Typ.Tstruct {name} => (fld, Typ.Tvar name, ann)
          | _ => fta
          }
      )
      fs;
  let fields =
    switch fields {
    | Some fields => Some (normalize_fields fields)
    | None => fields
    };
  let struct_typ =
    Typ.internal_mk_struct
      default::?default
      fields::?fields
      statics::?statics
      methods::?methods
      supers::?supers
      annots::?annots
      name;
  TypenameHash.replace tenv name struct_typ;
  struct_typ
};


/** Check if typename is found in tenv */
let mem tenv name => TypenameHash.mem tenv name;


/** Look up a name in the global type environment. */
let lookup tenv name =>
  try (Some (TypenameHash.find tenv name)) {
  | Not_found => None
  };


/** resolve a type string to a Java *class* type. For strings that may represent primitive or array
    typs, use [lookup_java_typ_from_string] */
let lookup_java_class_from_string tenv typ_str => lookup tenv (Typename.Java.from_string typ_str);


/** Lookup Java types by name */
let lookup_java_typ_from_string tenv typ_str => {
  let rec loop =
    fun
    | ""
    | "void" => Some Typ.Tvoid
    | "int" => Some (Typ.Tint Typ.IInt)
    | "byte" => Some (Typ.Tint Typ.IShort)
    | "short" => Some (Typ.Tint Typ.IShort)
    | "boolean" => Some (Typ.Tint Typ.IBool)
    | "char" => Some (Typ.Tint Typ.IChar)
    | "long" => Some (Typ.Tint Typ.ILong)
    | "float" => Some (Typ.Tfloat Typ.FFloat)
    | "double" => Some (Typ.Tfloat Typ.FDouble)
    | typ_str when String.contains typ_str '[' => {
        let stripped_typ = String.sub typ_str 0 (String.length typ_str - 2);
        switch (loop stripped_typ) {
        | Some typ => Some (Typ.Tptr (Typ.Tarray typ None) Typ.Pk_pointer)
        | None => None
        }
      }
    | typ_str =>
      /* non-primitive/non-array type--resolve it in the tenv */
      switch (lookup_java_class_from_string tenv typ_str) {
      | Some struct_typ => Some (Typ.Tstruct struct_typ)
      | None => None
      };
  loop typ_str
};


/** Add a (name,type) pair to the global type environment. */
let add tenv name struct_typ => TypenameHash.replace tenv name struct_typ;


/** Return the declaring class type of [pname_java] */
let proc_extract_declaring_class_typ tenv pname_java =>
  lookup_java_class_from_string tenv (Procname.java_get_class_name pname_java);


/** Return the return type of [pname_java]. */
let proc_extract_return_typ tenv pname_java =>
  lookup_java_typ_from_string tenv (Procname.java_get_return_type pname_java);


/** Get method that is being overriden by java_pname (if any) **/
let get_overriden_method tenv pname_java => {
  let struct_typ_get_method_by_name struct_typ method_name =>
    IList.find (fun meth => method_name == Procname.get_method meth) struct_typ.Typ.methods;
  let rec get_overriden_method_in_supers pname_java supers =>
    switch supers {
    | [superclass, ...supers_tail] =>
      switch (lookup tenv superclass) {
      | Some struct_typ =>
        try (Some (struct_typ_get_method_by_name struct_typ (Procname.java_get_method pname_java))) {
        | Not_found =>
          get_overriden_method_in_supers pname_java (supers_tail @ struct_typ.Typ.supers)
        }
      | None => get_overriden_method_in_supers pname_java supers_tail
      }
    | [] => None
    };
  switch (proc_extract_declaring_class_typ tenv pname_java) {
  | Some {Typ.supers: supers} => get_overriden_method_in_supers pname_java supers
  | _ => None
  }
};


/** expand a type if it is a typename by looking it up in the type environment */
let expand_type tenv (typ: Typ.t) =>
  switch typ {
  | Tvar tname =>
    switch (lookup tenv tname) {
    | Some struct_typ => Typ.Tstruct struct_typ
    | None => typ
    }
  | _ => typ
  };


/** expand a type if it is a (pointer to a) typename by looking it up in the type environment */
let expand_ptr_type tenv (typ: Typ.t) =>
  switch typ {
  | Tptr (Tvar tname) k =>
    switch (lookup tenv tname) {
    | Some struct_typ => Typ.Tptr (Tstruct struct_typ) k
    | None => typ
    }
  | _ => expand_type tenv typ
  };


/** Serializer for type environments */
let tenv_serializer: Serialization.serializer t = Serialization.create_serializer Serialization.tenv_key;

let global_tenv: Lazy.t (option t) =
  lazy (Serialization.from_file tenv_serializer DB.global_tenv_fname);


/** Load a type environment from a file */
let load_from_file (filename: DB.filename) :option t =>
  if (filename == DB.global_tenv_fname) {
    Lazy.force global_tenv
  } else {
    Serialization.from_file tenv_serializer filename
  };


/** Save a type environment into a file */
let store_to_file (filename: DB.filename) (tenv: t) =>
  Serialization.to_file tenv_serializer filename tenv;

let iter f tenv => TypenameHash.iter f tenv;

let fold f tenv => TypenameHash.fold f tenv;

let pp fmt (tenv: t) =>
  TypenameHash.iter
    (
      fun name typ => {
        Format.fprintf fmt "@[<6>NAME: %s@." (Typename.to_string name);
        Format.fprintf fmt "@[<6>TYPE: %a@." (Typ.pp_struct_typ pe_text (fun _ () => ())) typ
      }
    )
    tenv;
