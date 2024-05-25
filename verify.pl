% Hubert Michalski hm438596

% FIXME: REMOVE THIS
% FIXME: REMOVE RED CUTS
:- module(verify, [
    initMap/3,
    mapUpdate/4,
    mapGet/3,
    initList/3,
    listUpdate/4,
    initState/3,
    evalExpr/4,
    evalStmt/4,
    incrementIP/3,
    step/4
]).

:- ensure_loaded(library(lists)).
:- op(700, xfx, '<>').

% program(VarIdents, ArrIdents, Statements)
% VarIdents - list of variable identifiers
% ArrIdents - list of array identifiers
% Statements - list of statements

% state(VarMap, ArrMap, IPs)
% VarMap - list of pairs (VarId, Value)
% ArrMap - list of pairs (ArrId, [Value1, Value2, ...])
% IPs - list of instruction pointers

% ==== Program verification (mandatory functions) ====
% verify(+N, +FilePath)
verify(N, FilePath) :-
    integer(N),
    N >= 1,
    set_prolog_flag(fileerrors, off),
    ( see(FilePath) ->
        read(variables(VarIdents)),
        read(arrays(ArrIdents)),
        read(program(Statements)),
        seen,
        Program = program(VarIdents, ArrIdents, Statements),
        initState(Program, N, InitState),
        solveProgram(Program, InitState, N, Result),
        printResult(Result)
    ;
        format('Error: brak pliku o nazwie - ~w~n', FilePath)
    ).

verify(N, _) :-
    format('Error: parametr ~w powinien byc liczba > 0~n', N).

% initState(+Program, +N, -InitState)
initState(program(VarIdents, ArrIdents, _), N, state(VarMap, ArrMap, IPs)) :-
    initList(N, 1, IPs),
    initList(N, 0, InitArray),
    initMap(VarIdents, 0, VarMap),
    initMap(ArrIdents, InitArray, ArrMap).

% step(+Program, +State, +PrId, -NewState)
step(program(_, _, Statements), state(VarMap, ArrMap, IPs), PrId, NewState) :-
    nth0(PrId, IPs, IP),
    nth1(IP, Statements, Statement),
    evalStmt(Statement, state(VarMap, ArrMap, IPs), PrId, NewState).

% ==== Program verification (utility functions) ====
% solveProgram(+Program, +InitState, +N, -Result)
solveProgram(Program, InitState, N, Result) :-
    getProcessesInCriticalSection(Program, InitState, ProcessesInSection),
    length(ProcessesInSection, ProcessesInSectionLength),
    ( ProcessesInSectionLength > 1 ->
        Result = bad([], ProcessesInSection)
    ;
        traverse(Program, InitState, 0, N, [InitState], [], Result)
    ).

% traverse(+Program, +State, +PrId, +N, +Visited, +Path, -Result)
traverse(_, _, PrId, N, _, _, good) :- PrId >= N, !.
traverse(Program, State, PrId, N, Visited, Path, Result) :-
    ( step(Program, State, PrId, NewState) ->
        ( member(NewState, Visited) ->
            NextPrId is PrId + 1,
            traverse(Program, State, NextPrId, N, Visited, Path, Result)
        ;
            getIPForProcess(PrId, State, IP),
            append(Path, [PrId-IP], NewPath),
            getProcessesInCriticalSection(Program, NewState, ProcessesInSection),
            length(ProcessesInSection, ProcessesInSectionLength),
            ( ProcessesInSectionLength > 1 ->
                Result = bad(NewPath, ProcessesInSection)
            ;
                traverse(Program, NewState, 0, N, [NewState | Visited], NewPath, Result)
            )
        )
    ;
        NextPrId is PrId + 1,
        traverse(Program, State, NextPrId, N, Visited, Path, Result)
    ).

% getProcessesInCriticalSection(+Program, +State, -ProcessesInSection)
getProcessesInCriticalSection(program(_, _, Statements), state(_, _, IPs), ProcessesInSection) :-
    getProcessesInCriticalSection(IPs, Statements, 0, ProcessesInSection).

% getProcessesInCriticalSection(+IPs, +Statements, +Index, -ProcessesInSection)
getProcessesInCriticalSection([], _, _, []).
getProcessesInCriticalSection([IP | IPs], Statements, Index, [Index | ProcessesInSection]) :-
    nth1(IP, Statements, sekcja), !,
    NextIndex is Index + 1,
    getProcessesInCriticalSection(IPs, Statements, NextIndex, ProcessesInSection).
getProcessesInCriticalSection([_ | IPs], Statements, Index, ProcessesInSection) :-
    NextIndex is Index + 1,
    getProcessesInCriticalSection(IPs, Statements, NextIndex, ProcessesInSection).

% getIPForProcess(+PrId, +State, -IP)
getIPForProcess(PrId, state(_, _, IPs), IP) :-
    nth0(PrId, IPs, IP).

% printResult(+Result)
printResult(Result) :-
    ( Result = good ->
        format('Program jest poprawny (bezpieczny).~n')
    ;
        Result = bad(Path, ProcessesInSection),
        format('Program jest niepoprawny.~nNiepoprawny przeplot:~n'),
        printPath(Path),
        format('Procesy w sekcji:'),
        printProcesses(ProcessesInSection)
    ).

% printPath(+Path)
printPath([]).
printPath([PrId-IP | Rest]) :-
    format('    Proces ~d: ~d~n', [PrId, IP]),
    printPath(Rest).

% printProcesses(+PrIds)
printProcesses([PrId]) :-
   format(' ~d.~n', PrId).
printProcesses([PrId | PrIds]) :-
   format(' ~d,', PrId),
   printProcesses(PrIds).

% ==== Statement evaluation ====
% evalStmt(+Stmt, +State, +PrId, -NewState)
evalStmt(assign(VarId, Expr), state(VarMap, ArrMap, IPs), PrId, state(NewVarMap, ArrMap, NewIPs)) :-
    atom(VarId),
    evalExpr(Expr, state(VarMap, ArrMap, IPs), PrId, Value),
    mapUpdate(VarId, Value, VarMap, NewVarMap),
    incrementIP(PrId, IPs, NewIPs), !.

evalStmt(assign(array(ArrId, IndexExpr), Expr), state(VarMap, ArrMap, IPs), PrId, state(VarMap, NewArrMap, NewIPs)) :-
    evalExpr(IndexExpr, state(VarMap, ArrMap, IPs), PrId, Index),
    evalExpr(Expr, state(VarMap, ArrMap, IPs), PrId, Value),
    mapGet(ArrId, ArrMap, Array),
    listUpdate(Value, Index, Array, NewArray),
    mapUpdate(ArrId, NewArray, ArrMap, NewArrMap),
    incrementIP(PrId, IPs, NewIPs), !.

evalStmt(sekcja, state(VarMap, ArrMap, IPs), PrId, state(VarMap, ArrMap, NewIPs)) :-
    incrementIP(PrId, IPs, NewIPs), !.

evalStmt(goto(NewIP), state(VarMap, ArrMap, IPs), PrId, state(VarMap, ArrMap, NewIPs)) :-
    listUpdate(NewIP, PrId, IPs, NewIPs), !.

evalStmt(condGoto(BExpr, NewIP), state(VarMap, ArrMap, IPs), PrId, NewState) :-
    ( evalBExpr(BExpr, state(VarMap, ArrMap, IPs), PrId) ->
        evalStmt(goto(NewIP), state(VarMap, ArrMap, IPs), PrId, NewState)
    ;
        incrementIP(PrId, IPs, NewIPs),
        NewState = state(VarMap, ArrMap, NewIPs)
    ), !.

% incrementIP(+PrId, +IPs, -NewIPs)
incrementIP(PrId, IPs, NewIPs) :-
    nth0(PrId, IPs, IP),
    NewIP is IP + 1,
    listUpdate(NewIP, PrId, IPs, NewIPs).

% ==== Expression evaluation ====
% evalExpr(+Expr, +State, +PrId, -Value)
evalExpr(pid, _, PrId, PrId) :- !.

evalExpr(Num, _, _, Value) :-
    integer(Num),
    Value is Num, !.

evalExpr(VarId, state(VarMap, _, _), _, Value) :-
    atom(VarId),
    mapGet(VarId, VarMap, Value), !.

evalExpr(array(ArrId, IndexExpr), State, PrId, Value) :-
    evalExpr(IndexExpr, State, PrId, Index),
    State = state(_, ArrMap, _),
    mapGet(ArrId, ArrMap, Array),
    nth0(Index, Array, Value), !.

evalExpr(Expr, State, PrId, Value) :-
    Expr =.. [Op, Expr1, Expr2],
    member(Op, [+, -, *, /]),
    evalExpr(Expr1, State, PrId, Value1),
    evalExpr(Expr2, State, PrId, Value2),
    Eval =.. [Op, Value1, Value2],
    Value is Eval, !.

% evalBExpr(+BExpr, +State, +PrId)
evalBExpr(BExpr, State, PrId) :-
    BExpr =.. [Op, Expr1, Expr2],
    evalExpr(Expr1, State, PrId, Value1),
    evalExpr(Expr2, State, PrId, Value2),
    call(Op, Value1, Value2).

% ==== Utility functions ====
% initMap(+Idents, +Value, -Map)
initMap([], _, []).
initMap([Ident | RestIdents], Value, [(Ident, Value) | RestMap]) :-
    initMap(RestIdents, Value, RestMap).

% mapUpdate(+Key, +Value, +Map, -NewMap)
mapUpdate(Key, Value, [(Key, _) | Rest], [(Key, Value) | Rest]) :- !.
mapUpdate(Key, Value, [H | Rest], [H | NewRest]) :-
    mapUpdate(Key, Value, Rest, NewRest).

% mapGet(+Key, +Map, -Value)
mapGet(Key, Map, Value) :-
    member((Key, Value), Map), !.

% initList(+N, +Value, -List)
initList(0, _, []) :- !.
initList(N, Value, [Value | Rest]) :-
    N > 0,
    N1 is N - 1,
    initList(N1, Value, Rest).

% listUpdate(+Value, +Position, +List, -NewList)
listUpdate(Value, 0, [_ | Rest] ,[Value | Rest]) :- !.
listUpdate(Value, Position, [Start | Rest], [Start | NewRest]) :-
    Position > 0,
    NewPosition is Position - 1,
    listUpdate(Value, NewPosition, Rest, NewRest).
