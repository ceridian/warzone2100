SELECT DLookUp("[Research]![Deliverance Name]","[Research]","[Research]![ResearchID] = " & [Research ID]) AS Owner,
       DLookUp("[Structures]![Structure Name]","[Structures]","[Structures]![StructureID] =" & [StructureID]) AS Structure,
       [PR Structure List].StructureID
FROM Research INNER JOIN [PR Structure List] ON Research.ResearchID = [PR Structure List].[Research ID]
WHERE (((DLookUp(" [Research]![multiPlayer]","Research","Research![ResearchID] = " & [Research ID]))=Yes))
ORDER BY DLookUp("[Research]![Deliverance Name]","[Research]","[Research]![ResearchID] = " & [Research ID]);
