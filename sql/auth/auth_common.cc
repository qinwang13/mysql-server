/* Copyright (c) 2015, 2019, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include "sql/auth/auth_common.h"
#include "sql/auth/auth_utility.h"
#include "sql/auth/sql_auth_cache.h"

#include <string.h>
#include <cmath>
#include "sql/field.h"
#include "sql/table.h"
#include "sql_string.h"

namespace consts {
const std::string mysql("mysql");
const std::string system_user("SYSTEM_USER");
}  // namespace consts

/**
  Explicit Mem_root_base constructor.

  @param [in] mem_root MEM_ROOT handle

  If mem_root is provided, constructor initializes one
  and marks it to be freed as a part of destructor.
*/
Mem_root_base::Mem_root_base(MEM_ROOT *mem_root) : m_mem_root(mem_root) {
  m_inited = false;
  if (m_mem_root == nullptr) {
    m_mem_root = &m_internal_mem_root;
    init_sql_alloc(PSI_NOT_INSTRUMENTED, m_mem_root, ACL_ALLOC_BLOCK_SIZE, 0);
    m_inited = true;
  }
}

/** Meme_root_base constructor */
Mem_root_base::Mem_root_base() : Mem_root_base(nullptr) {}

/**
  Destructor.

  Frees MEM_ROOT if it was allocated as a part of constructor.
*/
Mem_root_base::~Mem_root_base() {
  if (m_inited) {
    free_root(m_mem_root, MYF(0));
    m_mem_root = nullptr;
    m_inited = false;
  }
}

bool User_table_schema_factory::is_old_user_table_schema(TABLE *table) {
  Field *password_field =
      table->field[User_table_old_schema::MYSQL_USER_FIELD_PASSWORD_56];
  return strncmp(password_field->field_name, "Password", 8) == 0;
}

Auth_id::Auth_id() {}

Auth_id::Auth_id(const char *user, size_t user_len, const char *host,
                 size_t host_len) {
  if (user != nullptr) m_user.assign(user, user_len);
  if (host != nullptr) m_host.assign(host, host_len);
}

Auth_id::Auth_id(const Auth_id_ref &id)
    : Auth_id(id.first.str, id.first.length, id.second.str, id.second.length) {}

Auth_id::Auth_id(const LEX_CSTRING &user, const LEX_CSTRING &host)
    : Auth_id(user.str, user.length, host.str, host.length) {}

Auth_id::Auth_id(const std::string &user, const std::string &host)
    : m_user(user), m_host(host) {}

Auth_id::Auth_id(const LEX_USER *lex_user)
    : Auth_id(lex_user->user, lex_user->host) {}

Auth_id::Auth_id(const ACL_USER *acl_user) {
  if (acl_user) {
    if (acl_user->user != nullptr)  // Not an anonymous user
      m_user.assign(acl_user->user, strlen(acl_user->user));
    m_host.assign(acl_user->host.get_host(), acl_user->host.get_host_len());
  }
}

Auth_id::~Auth_id() {}

Auth_id::Auth_id(const Auth_id &id) : m_user(id.m_user), m_host(id.m_host) {}

bool Auth_id::operator<(const Auth_id &id) const {
  if (m_user >= id.m_user) return m_host < id.m_host;
  return true;
}

void Auth_id::auth_str(std::string *out) const {
  String tmp;
  append_identifier(&tmp, m_user.c_str(), m_user.length());
  tmp.append('@');
  append_identifier(&tmp, m_host.c_str(), m_host.length());
  out->append(tmp.ptr());
}

std::string Auth_id::auth_str() const {
  String tmp;
  append_identifier(&tmp, m_user.c_str(), m_user.length());
  tmp.append('@');
  append_identifier(&tmp, m_host.c_str(), m_host.length());
  return tmp.ptr();
}

const std::string &Auth_id::user() const { return m_user; }
const std::string &Auth_id::host() const { return m_host; }

/**
  Converts privilege represented by LSB to string.

  This is used while serializing in-memory data to JSON format.

  @param [in,out] revoke_privs Privilege bitmask

  @returns Name for the privilege represented by LSB
*/
std::string get_one_priv(ulong &revoke_privs) {
  std::string priv;
  ulong lsb;
  if (revoke_privs != 0) {
    // find out the least significant bit(lsb)
    lsb = revoke_privs & ~(revoke_privs - 1);
    // reset the lsb
    revoke_privs &= ~lsb;
    // find out the position of the lsb
    size_t index = static_cast<size_t>(std::log2(lsb));
    // find the privilege string that corresponds to the lsb position
    if (global_acls_vector.size() >= index) {
      priv = global_acls_vector[index];
    }
  }
  return priv;
}