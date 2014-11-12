// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos-dbus-bindings/adaptor_generator.h"

#include <string>

#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/strings/string_utils.h>

#include "chromeos-dbus-bindings/dbus_signature.h"
#include "chromeos-dbus-bindings/indented_text.h"
#include "chromeos-dbus-bindings/interface.h"

using base::StringPrintf;
using std::string;
using std::vector;

namespace chromeos_dbus_bindings {

// static
bool AdaptorGenerator::GenerateAdaptors(
    const std::vector<Interface>& interfaces,
    const base::FilePath& output_file) {
  IndentedText text;
  CHECK(!interfaces.empty()) << "At least one interface must be provided";

  text.AddLine("// Automatic generation of D-Bus interfaces:");
  for (const auto& interface : interfaces) {
    text.AddLine(StringPrintf("//  - %s", interface.name.c_str()));
  }
  string header_guard = GenerateHeaderGuard(output_file, "");
  text.AddLine(StringPrintf("#ifndef %s", header_guard.c_str()));
  text.AddLine(StringPrintf("#define %s", header_guard.c_str()));
  text.AddLine("#include <memory>");
  text.AddLine("#include <string>");
  text.AddLine("#include <vector>");
  text.AddBlankLine();
  text.AddLine("#include <base/macros.h>");
  text.AddLine("#include <dbus/object_path.h>");
  text.AddLine("#include <chromeos/any.h>");
  text.AddLine("#include <chromeos/dbus/dbus_object.h>");
  text.AddLine("#include <chromeos/dbus/exported_object_manager.h>");
  text.AddLine("#include <chromeos/variant_dictionary.h>");

  for (const auto& interface : interfaces)
    GenerateInterfaceAdaptor(interface, &text);

  text.AddLine(StringPrintf("#endif  // %s", header_guard.c_str()));

  return WriteTextToFile(output_file, text);
}

// static
void AdaptorGenerator::GenerateInterfaceAdaptor(
    const Interface& interface,
    IndentedText *text) {
  vector<string> namespaces;
  string itf_name;
  CHECK(GetNamespacesAndClassName(interface.name, &namespaces, &itf_name));
  string class_name = itf_name + "Adaptor";
  string full_itf_name = GetFullClassName(namespaces, itf_name);
  itf_name += "Interface";

  text->AddBlankLine();
  for (const auto& ns : namespaces) {
    text->AddLine(StringPrintf("namespace %s {", ns.c_str()));
  }

  text->AddBlankLine();
  text->AddLine(StringPrintf("// Interface definition for %s.",
                             full_itf_name.c_str()));
  text->AddComments(interface.doc_string_);
  text->AddLine(StringPrintf("class %s {", itf_name.c_str()));
  text->AddLineWithOffset("public:", kScopeOffset);
  text->PushOffset(kBlockOffset);
  text->AddLine(StringPrintf("virtual ~%s() = default;", itf_name.c_str()));
  AddInterfaceMethods(interface, text);
  text->PopOffset();
  text->AddLine("};");

  text->AddBlankLine();
  text->AddLine(StringPrintf("// Interface adaptor for %s.",
                             full_itf_name.c_str()));
  text->AddLine(StringPrintf("class %s {", class_name.c_str()));
  text->AddLineWithOffset("public:", kScopeOffset);
  text->PushOffset(kBlockOffset);
  AddConstructor(class_name, itf_name, text);
  AddRegisterWithDBusObject(itf_name, interface, text);
  AddSendSignalMethods(interface, text);
  AddPropertyMethodImplementation(interface, text);
  text->PopOffset();

  text->AddBlankLine();
  text->AddLineWithOffset("private:", kScopeOffset);
  text->PushOffset(kBlockOffset);
  AddSignalDataMembers(interface, text);
  AddPropertyDataMembers(interface, text);

  text->AddLine(StringPrintf(
      "%s* interface_;  // Owned by container of this adapter.",
      itf_name.c_str()));

  text->AddBlankLine();
  text->AddLine(StringPrintf("DISALLOW_COPY_AND_ASSIGN(%s);",
                             class_name.c_str()));
  text->PopOffset();
  text->AddLine("};");

  text->AddBlankLine();
  for (auto it = namespaces.rbegin(); it != namespaces.rend(); ++it) {
    text->AddLine(StringPrintf("}  // namespace %s", it->c_str()));
  }
}

// static
void AdaptorGenerator::AddConstructor(const string& class_name,
                                      const string& itf_name,
                                      IndentedText *text) {
  text->AddLine(StringPrintf("%s(%s* interface) : interface_(interface) {}",
                             class_name.c_str(), itf_name.c_str()));
}

// static
void AdaptorGenerator::AddRegisterWithDBusObject(
    const std::string& itf_name,
    const Interface& interface,
    IndentedText *text) {
  text->AddBlankLine();
  text->AddLine(
    "void RegisterWithDBusObject(chromeos::dbus_utils::DBusObject* object) {");
  text->PushOffset(kBlockOffset);
  text->AddLine("chromeos::dbus_utils::DBusInterface* itf =");
  text->AddLineWithOffset(
      StringPrintf("object->AddOrGetInterface(\"%s\");",
                   interface.name.c_str()), kLineContinuationOffset);
  RegisterInterface(itf_name, interface, text);
  text->PopOffset();
  text->AddLine("}");
}

// static
void AdaptorGenerator::RegisterInterface(const string& itf_name,
                                         const Interface& interface,
                                         IndentedText *text) {
  if (!interface.methods.empty())
    text->AddBlankLine();
  for (const auto& method : interface.methods) {
    string add_handler_name;
    switch (method.kind) {
      case Interface::Method::Kind::kSimple:
        add_handler_name = "AddSimpleMethodHandler";
        break;
      case Interface::Method::Kind::kNormal:
        add_handler_name = "AddSimpleMethodHandlerWithError";
        break;
      case Interface::Method::Kind::kAsync:
        add_handler_name = "AddMethodHandler";
        break;
      case Interface::Method::Kind::kRaw:
        add_handler_name = "AddRawMethodHandler";
        break;
    }

    text->AddLine(StringPrintf("itf->%s(", add_handler_name.c_str()));
    text->PushOffset(kLineContinuationOffset);
    text->AddLine(StringPrintf("\"%s\",", method.name.c_str()));
    text->AddLine("base::Unretained(interface_),");
    text->AddLine(StringPrintf("&%s::%s);", itf_name.c_str(),
                               method.name.c_str()));
    text->PopOffset();
  }

  // Register signals.
  if (!interface.signals.empty())
    text->AddBlankLine();
  for (const auto& signal : interface.signals) {
    string signal_var_name = StringPrintf("signal_%s_", signal.name.c_str());
    string signal_type_name = StringPrintf("Signal%sType", signal.name.c_str());
    text->AddLine(StringPrintf("%s = itf->RegisterSignalOfType<%s>(\"%s\");",
                               signal_var_name.c_str(),
                               signal_type_name.c_str(),
                               signal.name.c_str()));
  }

  // Register exported properties.
  if (!interface.properties.empty())
    text->AddBlankLine();
  for (const auto& property : interface.properties) {
    string variable_name = GetPropertyVariableName(property.name);
    text->AddLine(StringPrintf("itf->AddProperty(\"%s\", &%s_);",
                               property.name.c_str(), variable_name.c_str()));
  }
}

// static
void AdaptorGenerator::AddInterfaceMethods(const Interface& interface,
                                           IndentedText *text) {
  IndentedText block;
  DbusSignature signature;
  if (!interface.methods.empty())
    block.AddBlankLine();

  for (const auto& method : interface.methods) {
    string const_method;
    if (method.is_const)
      const_method = " const";

    string return_type = "void";
    vector<string> method_params;
    auto input_arguments_copy = method.input_arguments;
    auto output_arguments_copy = method.output_arguments;
    switch (method.kind) {
      case Interface::Method::Kind::kSimple:
        if (output_arguments_copy.size() == 1) {
          CHECK(signature.Parse(output_arguments_copy[0].type, &return_type));
          output_arguments_copy.clear();
        }
        break;
      case Interface::Method::Kind::kNormal:
        method_params.push_back("chromeos::ErrorPtr* error");
        return_type = "bool";
        break;
      case Interface::Method::Kind::kAsync: {
        std::vector<std::string> out_types;
        for (const auto& argument : output_arguments_copy) {
          string param_type;
          CHECK(signature.Parse(argument.type, &param_type));
          out_types.push_back(param_type);
        }
        method_params.push_back(base::StringPrintf(
            "scoped_ptr<chromeos::dbus_utils::DBusMethodResponse<%s>> response",
             chromeos::string_utils::Join(", ", out_types).c_str()));
        output_arguments_copy.clear();
        break;
      }
      case Interface::Method::Kind::kRaw:
        method_params.push_back("dbus::MethodCall* method_call");
        method_params.push_back("chromeos::dbus_utils::ResponseSender sender");
        // Raw methods don't take static parameters or return values directly.
        input_arguments_copy.clear();
        output_arguments_copy.clear();
        break;
    }
    block.AddComments(method.doc_string_);
    string method_start = StringPrintf("virtual %s %s(",
                                       return_type.c_str(),
                                       method.name.c_str());
    string method_end = StringPrintf(")%s = 0;", const_method.c_str());
    int index = 0;
    for (const auto& argument : input_arguments_copy) {
      string param_type;
      CHECK(signature.Parse(argument.type, &param_type));
      if (!IsIntegralType(param_type)) {
        param_type = StringPrintf("const %s&", param_type.c_str());
      }
      string param_name = GetArgName("in", argument.name, ++index);
      method_params.push_back(param_type + ' ' + param_name);
    }

    for (const auto& argument : output_arguments_copy) {
      string param_type;
      CHECK(signature.Parse(argument.type, &param_type));
      string param_name = GetArgName("out", argument.name, ++index);
      method_params.push_back(param_type + "* " + param_name);
    }

    if (method_params.empty()) {
      block.AddLine(method_start + method_end);
    } else {
      block.AddLine(method_start);
      block.PushOffset(kLineContinuationOffset);
      for (size_t i = 0; i < method_params.size() - 1; i++)
        block.AddLine(method_params[i] + ',');
      block.AddLine(method_params.back() + method_end);
      block.PopOffset();
    }
  }
  text->AddBlock(block);
}

// static
void AdaptorGenerator::AddSendSignalMethods(
    const Interface& interface,
    IndentedText *text) {
  IndentedText block;
  DbusSignature signature;

  if (!interface.signals.empty())
    block.AddBlankLine();

  for (const auto& signal : interface.signals) {
    block.AddComments(signal.doc_string_);
    string method_start = StringPrintf("void Send%sSignal(",
                                       signal.name.c_str());
    string method_end = ") {";

    int index = 0;
    vector<string> method_params;
    vector<string> param_names;
    for (const auto& argument : signal.arguments) {
      string param_type;
      CHECK(signature.Parse(argument.type, &param_type));
      if (!IsIntegralType(param_type)) {
        param_type = StringPrintf("const %s&", param_type.c_str());
      }
      string param_name = GetArgName("in", argument.name, ++index);
      param_names.push_back(param_name);
      method_params.push_back(param_type + ' ' + param_name);
    }

    if (method_params.empty()) {
      block.AddLine(method_start + method_end);
    } else {
      block.AddLine(method_start);
      block.PushOffset(kLineContinuationOffset);
      for (size_t i = 0; i < method_params.size() - 1; i++)
        block.AddLine(method_params[i] + ',');
      block.AddLine(method_params.back() + method_end);
      block.PopOffset();
    }

    string args = chromeos::string_utils::Join(", ", param_names);
    block.PushOffset(kBlockOffset);
    block.AddLine(StringPrintf("auto signal = signal_%s_.lock();",
                                signal.name.c_str()));
    block.AddLine("if (signal)");
    block.AddLineWithOffset(StringPrintf("signal->Send(%s);", args.c_str()),
                            kBlockOffset);
    block.PopOffset();
    block.AddLine("}");
  }
  text->AddBlock(block);
}

// static
void AdaptorGenerator::AddSignalDataMembers(const Interface& interface,
                                            IndentedText *text) {
  IndentedText block;
  DbusSignature signature;

  for (const auto& signal : interface.signals) {
    string signal_type_name = StringPrintf("Signal%sType", signal.name.c_str());
    string signal_type_alias_begin =
        StringPrintf("using %s = chromeos::dbus_utils::DBusSignal<",
                     signal_type_name.c_str());
    string signal_type_alias_end = ">;";
    vector<string> params;
    for (const auto& argument : signal.arguments) {
      string param;
      CHECK(signature.Parse(argument.type, &param));
      if (!argument.name.empty())
        base::StringAppendF(&param, " /*%s*/", argument.name.c_str());
      params.push_back(param);
    }
    if (params.empty()) {
      block.AddLine(signal_type_alias_begin + signal_type_alias_end);
    } else {
      block.AddLine(signal_type_alias_begin);
      block.PushOffset(kLineContinuationOffset);
      for (size_t i = 0; i < params.size() - 1; i++)
        block.AddLine(params[i] + ',');
      block.AddLine(params.back() + signal_type_alias_end);
      block.PopOffset();
    }
    block.AddLine(
        StringPrintf("std::weak_ptr<%s> signal_%s_;",
                      signal_type_name.c_str(), signal.name.c_str()));
    block.AddBlankLine();
  }
  text->AddBlock(block);
}

// static
void AdaptorGenerator::AddPropertyMethodImplementation(
    const Interface& interface,
    IndentedText *text) {
  IndentedText block;
  DbusSignature signature;

  for (const auto& property : interface.properties) {
    block.AddBlankLine();
    string type;
    CHECK(signature.Parse(property.type, &type));
    string variable_name = GetPropertyVariableName(property.name);

    // Getter method.
    block.AddComments(property.doc_string_);
    block.AddLine(StringPrintf("%s Get%s() const {",
                               type.c_str(),
                               property.name.c_str()));
    block.PushOffset(kBlockOffset);
    block.AddLine(StringPrintf("return %s_.GetValue().Get<%s>();",
                               variable_name.c_str(),
                               type.c_str()));
    block.PopOffset();
    block.AddLine("}");

    // Setter method.
    if (!IsIntegralType(type))
      type = StringPrintf("const %s&", type.c_str());
    block.AddLine(StringPrintf("void Set%s(%s %s) {",
                               property.name.c_str(),
                               type.c_str(),
                               variable_name.c_str()));
    block.PushOffset(kBlockOffset);
    block.AddLine(StringPrintf("%s_.SetValue(%s);",
                               variable_name.c_str(),
                               variable_name.c_str()));
    block.PopOffset();
    block.AddLine("}");
  }
  text->AddBlock(block);
}

// static
void AdaptorGenerator::AddPropertyDataMembers(const Interface& interface,
                                              IndentedText *text) {
  IndentedText block;
  DbusSignature signature;

  for (const auto& property : interface.properties) {
    string type;
    CHECK(signature.Parse(property.type, &type));
    string variable_name = GetPropertyVariableName(property.name);

    block.AddLine(StringPrintf("chromeos::dbus_utils::ExportedProperty<%s>",
                               type.c_str()));
    block.PushOffset(kLineContinuationOffset);
    block.AddLine(StringPrintf("%s_;", variable_name.c_str()));
    block.PopOffset();
  }
  if (!interface.properties.empty())
    block.AddBlankLine();

  text->AddBlock(block);
}

// static
string AdaptorGenerator::GetPropertyVariableName(const string& property_name) {
  // Convert CamelCase property name to google_style variable name.
  string result;
  for (size_t i = 0; i < property_name.length(); i++) {
    char c = property_name[i];
    if (c < 'A' || c > 'Z') {
      result += c;
      continue;
    }

    if (i != 0) {
      result += '_';
    }
    result += base::ToLowerASCII(c);
  }
  return result;
}

}  // namespace chromeos_dbus_bindings
