// Copyright (c) 2026 OPEN CASCADE SAS
//
// This file is part of Open CASCADE Technology software library.
//
// This library is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License version 2.1 as published
// by the Free Software Foundation, with special exception defined in the file
// OCCT_LGPL_EXCEPTION.txt. Consult the file LICENSE_LGPL_21.txt included in OCCT
// distribution for complete text of the license and disclaimer of any warranty.
//
// Alternatively, this file may be used under the terms of Open CASCADE
// commercial license or contractual agreement.

#include <IFSelect_Selection.hxx>
#include <IFSelect_WorkSession.hxx>
#include <Interface_EntityIterator.hxx>
#include <Interface_Graph.hxx>
#include <Interface_InterfaceModel.hxx>
#include <Standard_Failure.hxx>
#include <TCollection_HAsciiString.hxx>

#include <gtest/gtest.h>

namespace
{

//! Smallest concrete model the work session will accept, so a graph can be computed.
class TestModel : public Interface_InterfaceModel
{
public:
  void ClearLabels() override {}

  void ClearHeader() override {}

  void GetFromAnother(const occ::handle<Interface_InterfaceModel>&) override {}

  occ::handle<Interface_InterfaceModel> NewEmptyModel() const override { return new TestModel; }

  void DumpHeader(Standard_OStream&, const int) const override {}

  void PrintLabel(const occ::handle<Standard_Transient>&,
                  Standard_OStream& theStream) const override
  {
    theStream << "test";
  }

  occ::handle<TCollection_HAsciiString> StringLabel(
    const occ::handle<Standard_Transient>&) const override
  {
    return new TCollection_HAsciiString("test");
  }
};

//! A selection whose evaluation always raises, so the caller's error-handling frame is observable:
//! guarded, the failure is caught and reported; unguarded, it escapes.
class ThrowingSelection : public IFSelect_Selection
{
public:
  Interface_EntityIterator RootResult(const Interface_Graph&) const override
  {
    throw Standard_Failure("deliberate failure from ThrowingSelection");
  }

  Interface_EntityIterator CompleteResult(const Interface_Graph& theGraph) const override
  {
    return RootResult(theGraph);
  }

  TCollection_AsciiString Label() const override
  {
    return TCollection_AsciiString("throwing selection");
  }

  void FillIterator(IFSelect_SelectionIterator&) const override {}
};

occ::handle<IFSelect_WorkSession> makeSessionWithGraph()
{
  occ::handle<IFSelect_WorkSession> aSession = new IFSelect_WorkSession;
  aSession->SetModel(new TestModel);
  return aSession;
}

} // namespace

// The error-handling frame is per session, not per process.
//
// IFSelect_WorkSession used a file-scope flag as the sentinel that makes each error-handled
// operation wrap itself in a try exactly once. Because it was shared, disabling error handling on
// one session cleared the sentinel for every other session in the process, so an unrelated
// session's failure escaped instead of being caught and reported.
TEST(IFSelect_WorkSessionTest, ErrorHandleIsPerSession)
{
  occ::handle<IFSelect_WorkSession> aDisabled = makeSessionWithGraph();
  occ::handle<IFSelect_WorkSession> aEnabled  = makeSessionWithGraph();

  ASSERT_TRUE(aEnabled->ErrorHandle());
  aDisabled->SetErrorHandle(false);
  ASSERT_FALSE(aDisabled->ErrorHandle());
  ASSERT_TRUE(aEnabled->ErrorHandle());

  // The second session still handles its own errors, despite the first having turned its own off.
  occ::handle<ThrowingSelection> aSelection = new ThrowingSelection;
  EXPECT_NO_THROW(aEnabled->EvalSelection(aSelection));
}

// Turning error handling off really does let the failure through, so the test above is asserting a
// working guard rather than an absent one.
TEST(IFSelect_WorkSessionTest, ErrorHandleOffLetsFailureEscape)
{
  occ::handle<IFSelect_WorkSession> aSession = makeSessionWithGraph();
  aSession->SetErrorHandle(false);

  occ::handle<ThrowingSelection> aSelection = new ThrowingSelection;
  EXPECT_THROW(aSession->EvalSelection(aSelection), Standard_Failure);
}
