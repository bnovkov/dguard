from element import Element
from random import sample

LABEL_SIZE = 10
def int2bin(number):
    return '{0:010b}'.format(number)

class System:
    def __init__(self, loadNodes, storeNodes):
        self.groups = [Group(i, []) for i in range(loadNodes)]
        self.labelCounter = 1
        self.usedLabels = []
        for i in range(storeNodes):
            node = Element(i)
            #print("kreiran element", node)
            groupId = sample(range(0, loadNodes), 2)
            #print("odabrani indexi grupa u koji ce se taj element dodati", groupId)
            for j in groupId:
                #print("\tdodavanje u grupu", j)
                #print("prije dodavanja\n", self.groups[j])
                

                self.groups[j].add(node)
                #print("nakon dodavanja\n", self.groups[j])




    def testHammingDistance(self, num, labels):
        hammingTest = self.hammingDistance(num, labels[0])
        for label in labels:
            if(hammingTest != self.hammingDistance(num, label)): return False
        return True

    def generateGroupLabel(self, group):
        
        labels = []
        for element in group.elements:
            if(element.label == ''): continue
            labels.append(element.label)
       
        if(len(labels) == 0):
            group.mainElement.setLabel(self.generateLabel())
            group.setTreshold(3)
            
            return True
        count = int2bin(labels[0]).count("1") % 2
        for label in labels:
            if(int2bin(labels[0]).count("1") % 2 != count): return False
        while(True):
            i = self.generateLabel()
            if(not self.testHammingDistance(i, labels)): continue
            group.mainElement.setLabel(i)
            group.setTreshold(self.hammingDistance(i, labels[0]))
            return True

    def generateGroupElementsLabel(self, group):
        
        for element in group.elements:
            if(element.label != ''): continue
            i = 1
            while(True):
                newLabel = int(int2bin(i) + int2bin(group.mainElement.label), 2)
                
                if(newLabel in self.usedLabels or self.hammingDistance(newLabel, group.mainElement.label) != group.treshold): 
                    i += 1
                    continue
                self.usedLabels.append(newLabel)
                element.setLabel(newLabel)
                break


    def labelGraph(self):
        
        repetition = len(self.groups)
        print(repetition)
        for j in range(repetition):
            smallestIndex = -1
            smallestSize = -1
            for i, group in enumerate(self.groups):
                if(smallestSize == -1 or (smallestSize > len(group.elements) and group.mainElement.label == '')):
                    smallestIndex = i
                    smallestSize = len(group.elements)
            
            if(not self.generateGroupLabel(self.groups[smallestIndex])):
                print("nemoguce")
                return 1
            self.generateGroupElementsLabel(self.groups[smallestIndex])
        



    def hammingDistance(self, num1, num2):
        return(bin(num1^num2).count("1"))


    def generateLabel(self):
        self.labelCounter += 1
        return self.labelCounter - 1
    



    def __str__(self) -> str:
        usedElements = []
        extraConnections = []
        ret = 'digraph G{\n'
        for group in self.groups:
            ret += "\tsubgraph cluster_{0} {{\n".format(int2bin((group.mainElement.label)))
            ret +="\t\tstyle=filled;\n\t\tcolor=lightgrey;\n\t\tnode [style=filled,color=white];\n"
            for element in group.elements:
                if(element.id not in usedElements):
                    ret += "\t\t{} -> {};\n".format(int2bin(element.label), int2bin(group.mainElement.label))
                    usedElements.append(int2bin(element.label))
                else:
                    extraConnections.append([int2bin(group.mainElement.label), int2bin(element.label)])
            
            ret+='\t\tlabel = \"podgrupa {};\"\n\t}}\n'.format(int2bin(group.mainElement.label))

        for connection in extraConnections:
            ret += "\t{} -> {};\n".format(connection[1], connection[0])
        ret += "}"
        return ret

class Group:
    def __init__(self, id, elements = []):
        self.mainElement = Element(id)
        self.elements = elements
        self.treshold = len(elements)
    def add(self, element):
        self.elements.append(element)
        self.treshold += 1
    def setTreshold(self, treshold):
        self.treshold = treshold

    def generateLabels(self, labels):
        #TODO treba iskonstruirati labelu load naredbe na temelju svih postavljenih store labela store naredbi iz proslih skupina
        pass

    def __str__(self) -> str:
        ret = str(self.mainElement.id)
        ret += "\n"
        for element in self.elements:
            ret += element.__str__() + " "
        return ret + "\n"
def invertedNumber(num):
    num2 = int2bin(num)
    num2 = num2[::-1]
    return int(num2, 2)
if __name__=='__main__':

    
    system = System(4, 7)

    system.labelGraph()
    for group in system.groups:
        print(group.mainElement.id, group.mainElement.label )
        for element in group.elements:
            print("\t", element.id , element.label, ",")
    print(system)

    





